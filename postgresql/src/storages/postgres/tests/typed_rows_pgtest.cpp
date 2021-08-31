#include <deque>
#include <list>
#include <set>

#include <storages/postgres/tests/util_pgtest.hpp>
#include <userver/storages/postgres/typed_result_set.hpp>

#include <userver/storages/postgres/io/boost_multiprecision.hpp>

namespace pg = storages::postgres;

namespace static_test {

using namespace pg::io::traits;

using MyTupleType = ::std::tuple<int, std::string, double>;

struct MyAggregateStruct {
  int int_member;
  std::string string_member;
  double double_member;
};

static_assert(boost::pfr::tuple_size_v<MyAggregateStruct> == 3);

struct MyStructWithOptional {
  std::optional<int> int_member;
  std::optional<std::string> string_member;
  std::optional<double> double_member;
};

static_assert(boost::pfr::tuple_size_v<MyStructWithOptional> == 3);

class MyIntrusiveClass {
  int int_member_;
  std::string string_member_;
  double double_member_;

 public:
  auto Introspect() {
    return std::tie(int_member_, string_member_, double_member_);
  }

  int GetInt() const { return int_member_; }
  const std::string& GetString() const { return string_member_; }
  double GetDouble() const { return double_member_; }
};

struct MyPolymorphicBase {
  virtual ~MyPolymorphicBase() {}
};

struct MyPolymorphicDerived : MyPolymorphicBase {};

class MyPolymorphicInrospected : public MyPolymorphicBase {
  int int_member_;
  std::string string_member_;
  double double_member_;

 public:
  auto Introspect() {
    return std::tie(int_member_, string_member_, double_member_);
  }
};

static_assert(kRowCategory<std::string> == RowCategoryType::kNonRow);
static_assert(kRowCategory<std::vector<std::string>> ==
              RowCategoryType::kNonRow);
static_assert(kRowCategory<pg::MultiPrecision<50>> == RowCategoryType::kNonRow);

static_assert(kRowCategory<MyTupleType> == RowCategoryType::kTuple);
static_assert(kRowCategory<MyAggregateStruct> == RowCategoryType::kAggregate);
static_assert(kRowCategory<MyIntrusiveClass> ==
              RowCategoryType::kIntrusiveIntrospection);
static_assert(kRowCategory<MyPolymorphicBase> == RowCategoryType::kNonRow);
static_assert(kRowCategory<MyPolymorphicDerived> == RowCategoryType::kNonRow);
static_assert(kRowCategory<MyPolymorphicInrospected> ==
              RowCategoryType::kIntrusiveIntrospection);

}  // namespace static_test

namespace {

UTEST_F(PostgreConnection, TypedResult) {
  using MyTuple = static_test::MyTupleType;
  using MyStruct = static_test::MyAggregateStruct;
  using MyClass = static_test::MyIntrusiveClass;

  using MyTuples = std::vector<MyTuple>;
  using MyStructs = std::list<MyStruct>;
  using MyClasses = std::deque<MyClass>;

  CheckConnection(conn);
  pg::ResultSet res{nullptr};

  EXPECT_NO_THROW(res = conn->Execute("select $1, $2, $3", 42, "foobar", 3.14));
  ASSERT_FALSE(res.IsEmpty());

  EXPECT_THROW(res.AsSetOf<int>(), pg::NonSingleColumResultSet);

  EXPECT_THROW(res.AsSetOf<MyTuple>(), pg::NonSingleColumResultSet);
  EXPECT_THROW(res.AsSetOf<MyStruct>(), pg::NonSingleColumResultSet);
  EXPECT_THROW(res.AsSetOf<MyClass>(), pg::NonSingleColumResultSet);

  auto tuples_res = res.AsSetOf<MyTuple>(pg::kRowTag);
  auto t = tuples_res[0];
  EXPECT_EQ(42, std::get<0>(t));
  EXPECT_EQ("foobar", std::get<1>(t));
  EXPECT_EQ(3.14, std::get<2>(t));

  auto struct_res = res.AsSetOf<MyStruct>(pg::kRowTag);
  auto s = struct_res[0];
  EXPECT_EQ(42, s.int_member);
  EXPECT_EQ("foobar", s.string_member);
  EXPECT_EQ(3.14, s.double_member);

  auto class_res = res.AsSetOf<MyClass>(pg::kRowTag);
  auto c = class_res[0];
  EXPECT_EQ(42, c.GetInt());
  EXPECT_EQ("foobar", c.GetString());
  EXPECT_EQ(3.14, c.GetDouble());

  auto tuples = res.AsContainer<MyTuples>(pg::kRowTag);
  EXPECT_EQ(res.Size(), tuples.size());
  auto structs = res.AsContainer<MyStructs>(pg::kRowTag);
  EXPECT_EQ(res.Size(), structs.size());
  auto classes = res.AsContainer<MyClasses>(pg::kRowTag);
  EXPECT_EQ(res.Size(), classes.size());

  auto tuple_set = res.AsContainer<std::set<MyTuple>>(pg::kRowTag);
  EXPECT_EQ(res.Size(), tuple_set.size());

  EXPECT_NO_THROW(res.AsSingleRow<MyStruct>(pg::kRowTag));
  EXPECT_NO_THROW(res.AsSingleRow<MyClass>(pg::kRowTag));
  EXPECT_NO_THROW(res.AsSingleRow<MyTuple>(pg::kRowTag));
}

UTEST_F(PostgreConnection, OptionalFields) {
  using MyStruct = static_test::MyStructWithOptional;

  CheckConnection(conn);
  pg::ResultSet res{nullptr};

  EXPECT_NO_THROW(res = conn->Execute("select 1, 'aa', null"));
  EXPECT_NO_THROW(res.AsSingleRow<MyStruct>(pg::kRowTag));
}

UTEST_F(PostgreConnection, EmptyTypedResult) {
  using MyTuple = static_test::MyTupleType;
  using MyStruct = static_test::MyAggregateStruct;
  using MyClass = static_test::MyIntrusiveClass;

  CheckConnection(conn);
  auto empty_res =
      conn->Execute("select $1, $2, $3 limit 0", 42, "foobar", 3.14);
  EXPECT_THROW(empty_res.AsSingleRow<MyStruct>(), pg::NonSingleRowResultSet);
  EXPECT_THROW(empty_res.AsSingleRow<MyClass>(), pg::NonSingleRowResultSet);
  EXPECT_THROW(empty_res.AsSingleRow<MyTuple>(), pg::NonSingleRowResultSet);
}

UTEST_F(PostgreConnection, TypedResultOobAccess) {
  using MyTuple = static_test::MyTupleType;
  using MyStruct = static_test::MyAggregateStruct;
  using MyClass = static_test::MyIntrusiveClass;

  CheckConnection(conn);
  pg::ResultSet res{nullptr};

  EXPECT_NO_THROW(res = conn->Execute("select $1, $2, $3", 42, "foobar", 3.14));

  auto tuples_res = res.AsSetOf<MyTuple>(pg::kRowTag);
  ASSERT_EQ(1, tuples_res.Size());
  EXPECT_NO_THROW(tuples_res[0]);
  EXPECT_THROW(tuples_res[1], pg::RowIndexOutOfBounds);

  auto struct_res = res.AsSetOf<MyStruct>(pg::kRowTag);
  ASSERT_EQ(1, struct_res.Size());
  EXPECT_NO_THROW(struct_res[0]);
  EXPECT_THROW(struct_res[1], pg::RowIndexOutOfBounds);

  auto class_res = res.AsSetOf<MyClass>(pg::kRowTag);
  ASSERT_EQ(1, class_res.Size());
  EXPECT_NO_THROW(class_res[0]);
  EXPECT_THROW(class_res[1], pg::RowIndexOutOfBounds);
}

}  // namespace
