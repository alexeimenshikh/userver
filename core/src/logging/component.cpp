#include <userver/logging/component.hpp>

#include <chrono>
#include <stdexcept>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem/operations.hpp>

#include <logging/config.hpp>
#include <logging/impl/buffered_file_sink.hpp>
#include <logging/impl/fd_sink.hpp>
#include <logging/impl/tcp_socket_sink.hpp>
#include <logging/impl/unix_socket_sink.hpp>
#include <logging/tp_logger.hpp>
#include <userver/components/component.hpp>
#include <userver/components/statistics_storage.hpp>
#include <userver/engine/async.hpp>
#include <userver/engine/sleep.hpp>
#include <userver/logging/format.hpp>
#include <userver/logging/log.hpp>
#include <userver/logging/logger.hpp>
#include <userver/net/blocking/get_addr_info.hpp>
#include <userver/os_signals/component.hpp>
#include <userver/utils/algo.hpp>
#include <userver/utils/statistics/writer.hpp>
#include <userver/utils/thread_name.hpp>
#include <userver/yaml_config/map_to_array.hpp>
#include <userver/yaml_config/merge_schemas.hpp>

USERVER_NAMESPACE_BEGIN

namespace components {

namespace {

constexpr std::chrono::seconds kDefaultFlushInterval{2};
constexpr std::string_view kUnixSocketPrefix = "unix:";

void ReopenAll(const std::shared_ptr<logging::impl::TpLogger>& logger) {
  logger->Reopen(logging::impl::ReopenMode::kAppend);
}

void CreateLogDirectory(const std::string& logger_name,
                        const std::string& file_path) {
  try {
    const auto dirname = boost::filesystem::path(file_path).parent_path();
    boost::filesystem::create_directories(dirname);
  } catch (const std::exception& e) {
    auto msg = "Failed to create directory for log file of logger '" +
               logger_name + "': " + e.what();
    LOG_ERROR() << msg;
    throw std::runtime_error(msg);
  }
}

logging::impl::SinkPtr GetSinkFromFilename(
    const spdlog::filename_t& file_path) {
  if (boost::starts_with(file_path, kUnixSocketPrefix)) {
    // Use Unix-socket sink
    return std::make_unique<logging::impl::UnixSocketSink>(
        file_path.substr(kUnixSocketPrefix.size()));
  } else {
    return std::make_unique<logging::impl::BufferedFileSink>(file_path);
  }
}

logging::impl::SinkPtr MakeOptionalSink(const logging::LoggerConfig& config) {
  if (config.file_path == "@null") {
    return nullptr;
  } else if (config.file_path == "@stderr") {
    return std::make_unique<logging::impl::BufferedUnownedFileSink>(stderr);
  } else if (config.file_path == "@stdout") {
    return std::make_unique<logging::impl::BufferedUnownedFileSink>(stdout);
  } else {
    CreateLogDirectory(config.logger_name, config.file_path);
    return GetSinkFromFilename(config.file_path);
  }
}

auto MakeTestsuiteSink(const logging::TestsuiteCaptureConfig& config) {
  auto addrs = net::blocking::GetAddrInfo(config.host,
                                          std::to_string(config.port).c_str());
  return std::make_unique<logging::impl::TcpSocketSink>(std::move(addrs));
}

std::shared_ptr<logging::impl::TpLogger> MakeLogger(
    const logging::LoggerConfig& config,
    logging::impl::TcpSocketSink*& socket_sink) {
  auto logger = std::make_shared<logging::impl::TpLogger>(config.format,
                                                          config.logger_name);
  logger->SetLevel(config.level);
  logger->SetFlushOn(config.flush_level);

  if (auto basic_sink = MakeOptionalSink(config)) {
    logger->AddSink(std::move(basic_sink));
  }

  if (config.testsuite_capture) {
    auto socket_sink_holder = MakeTestsuiteSink(*config.testsuite_capture);
    socket_sink = socket_sink_holder.get();
    logger->AddSink(std::move(socket_sink_holder));
    // Overwriting the level of TpLogger.
    socket_sink->SetLevel(logging::Level::kNone);
  }

  return logger;
}

}  // namespace

/// [Signals sample - init]
Logging::Logging(const ComponentConfig& config, const ComponentContext& context)
    : signal_subscriber_(context.FindComponent<os_signals::ProcessorComponent>()
                             .Get()
                             .AddListener(this, kName, os_signals::kSigUsr1,
                                          &Logging::OnLogRotate))
/// [Signals sample - init]
{
  auto& storage =
      context.FindComponent<components::StatisticsStorage>().GetStorage();
  statistics_holder_ = storage.RegisterWriter(
      "logger", [this](utils::statistics::Writer& writer) {
        return WriteStatistics(writer);
      });

  try {
    Init(config, context);
  } catch (const std::exception&) {
    Stop();
    throw;
  }
}

void Logging::Init(const ComponentConfig& config,
                   const ComponentContext& context) {
  const auto fs_task_processor_name =
      config["fs-task-processor"].As<std::string>();
  fs_task_processor_ = &context.GetTaskProcessor(fs_task_processor_name);

  const auto logger_configs =
      yaml_config::ParseMapToArray<logging::LoggerConfig>(config["loggers"]);

  for (const auto& logger_config : logger_configs) {
    const bool is_default_logger = (logger_config.logger_name == "default");

    const auto tp_name =
        logger_config.fs_task_processor.value_or(fs_task_processor_name);

    if (logger_config.testsuite_capture && !is_default_logger) {
      throw std::runtime_error(
          "Testsuite capture can only currently be enabled "
          "for the default logger");
    }

    auto logger = MakeLogger(logger_config, socket_sink_);

    if (is_default_logger) {
      if (logger_config.queue_overflow_behavior ==
          logging::QueueOverflowBehavior::kBlock) {
        throw std::runtime_error(
            "'default' logger should not be set to 'overflow_behavior: block'! "
            "Default logger is used by the userver internals, including the "
            "logging internals. Blocking inside the engine internals could "
            "lead to hardly reproducible hangups in some border cases of error "
            "reporting.");
      }

      logging::LogFlush();
      logging::impl::SetDefaultLoggerRef(*logger);

      // the default logger should outlive the component
      static logging::LoggerPtr default_component_logger_holder{};
      default_component_logger_holder = logger;
    }

    logger->StartConsumerTask(context.GetTaskProcessor(tp_name),
                              logger_config.message_queue_size,
                              logger_config.queue_overflow_behavior);

    auto insertion_result =
        loggers_.emplace(logger_config.logger_name, std::move(logger));
    if (!insertion_result.second) {
      throw std::runtime_error("duplicate logger '" +
                               insertion_result.first->first + '\'');
    }
  }
  flush_task_.Start("log_flusher",
                    utils::PeriodicTask::Settings(
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            kDefaultFlushInterval),
                        {}, logging::Level::kTrace),
                    [this] { FlushLogs(); });
}

Logging::~Logging() { Stop(); }

void Logging::Stop() noexcept {
  /// [Signals sample - destr]
  signal_subscriber_.Unsubscribe();
  /// [Signals sample - destr]
  flush_task_.Stop();

  // Loggers could be used from non coroutine environments and should be
  // available even after task processors are down.
  for (const auto& [logger_name, logger] : loggers_) {
    logger->StopConsumerTask();
  }
}

logging::LoggerPtr Logging::GetLogger(const std::string& name) {
  auto it = loggers_.find(name);
  if (it == loggers_.end()) {
    throw std::runtime_error("logger '" + name + "' not found");
  }
  return it->second;
}

logging::LoggerPtr Logging::GetLoggerOptional(const std::string& name) {
  return utils::FindOrDefault(loggers_, name, nullptr);
}

void Logging::StartSocketLoggingDebug() {
  UASSERT(socket_sink_);
  socket_sink_->SetLevel(logging::Level::kTrace);
}

void Logging::StopSocketLoggingDebug() {
  UASSERT(socket_sink_);
  logging::LogFlush();
  socket_sink_->SetLevel(logging::Level::kNone);
  socket_sink_->Close();
}

void Logging::OnLogRotate() {
  try {
    TryReopenFiles();

  } catch (const std::exception& e) {
    LOG_ERROR() << "An error occurred while ReopenAll: " << e;
  }
}

void Logging::TryReopenFiles() {
  std::vector<engine::TaskWithResult<void>> tasks;
  tasks.reserve(loggers_.size() + 1);
  for (const auto& item : loggers_) {
    tasks.push_back(engine::CriticalAsyncNoSpan(*fs_task_processor_, ReopenAll,
                                                item.second));
  }

  std::string result_messages;

  for (auto& task : tasks) {
    try {
      task.Get();
    } catch (const std::exception& e) {
      result_messages += e.what();
      result_messages += ";";
    }
  }
  LOG_INFO() << "Log rotated";

  if (!result_messages.empty()) {
    throw std::runtime_error("ReopenAll errors: " + result_messages);
  }
}

void Logging::WriteStatistics(utils::statistics::Writer& writer) const {
  for (const auto& [name, logger] : loggers_) {
    writer.ValueWithLabels(logger->GetStatistics(),
                           {"logger", logger->GetLoggerName()});
  }
}

void Logging::FlushLogs() {
  logging::LogFlush();
  for (auto& item : loggers_) {
    item.second->Flush();
  }
}

yaml_config::Schema Logging::GetStaticConfigSchema() {
  return yaml_config::MergeSchemas<impl::ComponentBase>(R"(
type: object
description: Logging component
additionalProperties: false
properties:
    fs-task-processor:
        type: string
        description: task processor for disk I/O operations
    loggers:
        type: object
        description: logger options
        properties: {}
        additionalProperties:
            type: object
            description: logger options
            additionalProperties: false
            properties:
                file_path:
                    type: string
                    description: path to the log file
                level:
                    type: string
                    description: log verbosity
                    defaultDescription: info
                format:
                    type: string
                    description: log output format
                    defaultDescription: tskv
                    enum:
                      - tskv
                      - ltsv
                      - raw
                flush_level:
                    type: string
                    description: messages of this and higher levels get flushed to the file immediately
                    defaultDescription: warning
                message_queue_size:
                    type: integer
                    description: the size of internal message queue, must be a power of 2
                    defaultDescription: 65536
                overflow_behavior:
                    type: string
                    description: "message handling policy while the queue is full: `discard` drops messages, `block` waits until message gets into the queue"
                    defaultDescription: discard
                    enum:
                      - discard
                      - block
                fs-task-processor:
                    type: string
                    description: task processor for disk I/O operations for this logger
                    defaultDescription: fs-task-processor of the loggers component
                testsuite-capture:
                    type: object
                    description: if exists, setups additional TCP log sink for testing purposes
                    defaultDescription: "{}"
                    additionalProperties: false
                    properties:
                        host:
                            type: string
                            description: testsuite hostname, e.g. localhost
                        port:
                            type: integer
                            description: testsuite port
)");
}

}  // namespace components

USERVER_NAMESPACE_END
