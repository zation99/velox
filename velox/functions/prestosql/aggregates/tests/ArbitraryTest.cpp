/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "velox/exec/Spill.h"
#include "velox/exec/tests/utils/PlanBuilder.h"
#include "velox/exec/tests/utils/TempDirectoryPath.h"
#include "velox/functions/lib/aggregates/tests/utils/AggregationTestBase.h"
#include "velox/functions/lib/window/tests/WindowTestBase.h"

using namespace facebook::velox::exec::test;
using namespace facebook::velox::functions::aggregate::test;
using namespace facebook::velox::window::test;

namespace facebook::velox::aggregate::test {

namespace {

class ArbitraryTest : public AggregationTestBase {};

TEST_F(ArbitraryTest, noNulls) {
  // Create vectors without nulls because DuckDB's "first" aggregate does not
  // ignore them.
  const int32_t size = 10'000;
  auto vectors = {makeRowVector(
      {makeFlatVector<int64_t>(size, [](vector_size_t row) { return row; }),
       makeFlatVector<int8_t>(size, [](vector_size_t row) { return row; }),
       makeFlatVector<int16_t>(size, [](vector_size_t row) { return row; }),
       makeFlatVector<int32_t>(size, [](vector_size_t row) { return row; }),
       makeFlatVector<int64_t>(size, [](vector_size_t row) { return row; }),
       makeFlatVector<float>(size, [](vector_size_t row) { return row; }),
       makeFlatVector<double>(size, [](vector_size_t row) { return row; })})};
  createDuckDbTable(vectors);

  std::vector<std::string> aggregates = {
      "arbitrary(c1)",
      "arbitrary(c2)",
      "any_value(c3)",
      "arbitrary(c4)",
      "arbitrary(c5)",
      "any_value(c6)"};

  // We do not test with TableScan because having two input splits makes the
  // result non-deterministic.
  // Global aggregation.
  testAggregations(
      vectors,
      {},
      aggregates,
      "SELECT first(c1), first(c2), first(c3), first(c4), first(c5), first(c6) FROM tmp");

  // Group by aggregation.
  testAggregations(
      [&](PlanBuilder& builder) {
        builder.values(vectors).project(
            {"c0 % 10", "c1", "c2", "c3", "c4", "c5", "c6"});
      },
      {"p0"},
      aggregates,
      "SELECT c0 % 10, first(c1), first(c2), first(c3), first(c4), first(c5), first(c6) FROM tmp GROUP BY 1");

  // encodings: use filter to wrap aggregation inputs in a dictionary.
  testAggregations(
      [&](PlanBuilder& builder) {
        builder.values(vectors)
            .filter("c0 % 2 = 0")
            .project({"c0 % 10", "c1", "c2", "c3", "c4", "c5", "c6"});
      },
      {"p0"},
      aggregates,
      "SELECT c0 % 10, first(c1), first(c2), first(c3), first(c4), first(c5), first(c6) FROM tmp WHERE c0 % 2 = 0 GROUP BY 1");

  testAggregations(
      [&](PlanBuilder& builder) {
        builder.values(vectors).filter("c0 % 2 = 0");
      },
      {},
      aggregates,
      "SELECT first(c1), first(c2), first(c3), first(c4), first(c5), first(c6) FROM tmp WHERE c0 % 2 = 0");
}

TEST_F(ArbitraryTest, nulls) {
  auto vectors = {
      makeRowVector(
          {makeNullableFlatVector<int32_t>({1, 1, 2, 2, 3, 3}),
           makeNullableFlatVector<int64_t>(
               {std::nullopt, std::nullopt, std::nullopt, 4, std::nullopt, 5}),
           makeNullableFlatVector<double>({
               std::nullopt,
               0.50,
               std::nullopt,
               std::nullopt,
               0.25,
               std::nullopt,
           }),
           makeNullConstant(TypeKind::UNKNOWN, 6)}),
  };

  // We do not test with TableScan because having two input splits makes the
  // result non-deterministic. Also, unknown type is not supported in Writer
  // yet. Global aggregation.
  testAggregations(
      vectors,
      {},
      {"arbitrary(c1)", "arbitrary(c2)", "arbitrary(c3)"},
      "SELECT * FROM( VALUES (4, 0.50, NULL)) AS t");

  // Group by aggregation.
  testAggregations(
      vectors,
      {"c0"},
      {"arbitrary(c1)", "arbitrary(c2)", "arbitrary(c3)"},
      "SELECT * FROM(VALUES (1, NULL, 0.50, NULL), (2, 4, NULL, NULL), (3, 5, 0.25, NULL)) AS t");
}

TEST_F(ArbitraryTest, varchar) {
  auto rowType = ROW({"c0", "c1"}, {INTEGER(), VARCHAR()});
  auto vectors = makeVectors(rowType, 1000, 10);
  createDuckDbTable(vectors);

  // We do not test with TableScan because having two input splits makes the
  // result non-deterministic.
  testAggregations(
      [&](PlanBuilder& builder) {
        builder.values(vectors).project({"c0 % 11", "c1"});
      },
      {"p0"},
      {"arbitrary(c1)"},
      "SELECT c0 % 11, first(c1) FROM tmp WHERE c1 IS NOT NULL GROUP BY 1");

  testAggregations(
      vectors,
      {},
      {"arbitrary(c1)"},
      "SELECT first(c1) FROM tmp WHERE c1 IS NOT NULL");

  // encodings: use filter to wrap aggregation inputs in a dictionary.
  testAggregations(
      [&](PlanBuilder& builder) {
        builder.values(vectors).filter("c0 % 2 = 0").project({"c0 % 11", "c1"});
      },
      {"p0"},
      {"arbitrary(c1)"},
      "SELECT c0 % 11, first(c1) FROM tmp WHERE c0 % 2 = 0 AND c1 IS NOT NULL GROUP BY 1");

  testAggregations(
      [&](PlanBuilder& builder) {
        builder.values(vectors).filter("c0 % 2 = 0");
      },
      {},
      {"arbitrary(c1)"},
      "SELECT first(c1) FROM tmp WHERE c0 % 2 = 0 AND c1 IS NOT NULL");
}

TEST_F(ArbitraryTest, varcharConstAndNulls) {
  auto vectors = {makeRowVector({
      makeFlatVector<int32_t>(100, [](auto row) { return row % 7; }),
      makeConstant("apple", 100),
      makeNullConstant(TypeKind::VARCHAR, 100),
  })};

  createDuckDbTable(vectors);

  testAggregations(
      vectors,
      {},
      {"arbitrary(c1)", "arbitrary(c2)"},
      "SELECT first(c1), first(c2) FROM tmp");

  testAggregations(
      vectors,
      {"c0"},
      {"arbitrary(c1)", "arbitrary(c2)"},
      "SELECT c0, first(c1), first(c2) FROM tmp group by c0");
}

TEST_F(ArbitraryTest, numericConstAndNulls) {
  auto vectors = {makeRowVector(
      {makeFlatVector<int32_t>(100, [](auto row) { return row % 7; }),
       makeConstant(11, 100),
       makeNullConstant(TypeKind::BIGINT, 100)})};

  createDuckDbTable(vectors);

  testAggregations(
      vectors,
      {},
      {"arbitrary(c1)", "arbitrary(c2)"},
      "SELECT first(c1), first(c2) FROM tmp");

  testAggregations(
      vectors,
      {"c0"},
      {"arbitrary(c1)", "arbitrary(c2)"},
      "SELECT c0, first(c1), first(c2) FROM tmp group by c0");
}

TEST_F(ArbitraryTest, boolean) {
  auto data = makeRowVector({
      // Grouping key.
      makeFlatVector<int64_t>({1, 1, 2, 2, 3, 3, 4, 4}),
      // Input values: 'constant' within groups.
      makeNullableFlatVector<bool>(
          {true,
           true,
           false,
           false,
           std::nullopt,
           std::nullopt,
           std::nullopt,
           false}),
      makeConstant<bool>(std::nullopt, 8),
  });

  auto expectedResult = makeRowVector({
      makeFlatVector<int64_t>({1, 2, 3, 4}),
      makeNullableFlatVector<bool>({true, false, std::nullopt, false}),
  });

  testAggregations({data}, {"c0"}, {"arbitrary(c1)"}, {expectedResult});

  // Global aggregation.
  auto plan = PlanBuilder()
                  .values({data})
                  .singleAggregation({}, {"arbitrary(c1)"})
                  .planNode();

  assertQuery(plan, "SELECT true");

  testAggregations({data}, {}, {"arbitrary(c2)"}, "SELECT null");
}

TEST_F(ArbitraryTest, timestamp) {
  auto data = makeRowVector({
      // Grouping key.
      makeFlatVector<int64_t>({1, 1, 2, 2, 3, 3, 4, 4}),
      // Input values: constant within groups: 100.1, 100.1, 200.2, 200.2, etc.
      makeNullableFlatVector<Timestamp>(
          {Timestamp(100, 1),
           Timestamp(100, 1),
           Timestamp(200, 2),
           Timestamp(200, 2),
           std::nullopt,
           std::nullopt,
           std::nullopt,
           Timestamp(100, 4)}),
      makeConstant<Timestamp>(std::nullopt, 8),
  });

  auto expectedResult = makeRowVector({
      makeFlatVector<int64_t>({1, 2, 3, 4}),
      makeNullableFlatVector<Timestamp>(
          {Timestamp(100, 1),
           Timestamp(200, 2),
           std::nullopt,
           Timestamp(100, 4)}),
  });

  testAggregations({data}, {"c0"}, {"arbitrary(c1)"}, {expectedResult});

  // Global aggregation.
  auto plan = PlanBuilder()
                  .values({data})
                  .singleAggregation({}, {"arbitrary(c1)"})
                  .planNode();

  auto result = readSingleValue(plan);
  ASSERT_TRUE(!result.isNull());
  ASSERT_EQ(result.kind(), TypeKind::TIMESTAMP);

  auto timestamp = result.value<Timestamp>();
  ASSERT_EQ(timestamp, Timestamp(100, 1));

  testAggregations({data}, {}, {"arbitrary(c2)"}, "SELECT null");
}

TEST_F(ArbitraryTest, date) {
  auto data = makeRowVector({
      // Grouping key.
      makeFlatVector<int64_t>({1, 1, 2, 2, 3, 3, 4, 4}),
      // Input values: constant within groups.
      makeNullableFlatVector<int32_t>(
          {125, 125, 126, 126, std::nullopt, std::nullopt, std::nullopt, 128},
          DATE()),
      makeConstant<Timestamp>(std::nullopt, 8),
  });

  auto expectedResult = makeRowVector({
      makeFlatVector<int64_t>({1, 2, 3, 4}),
      makeNullableFlatVector<int32_t>({125, 126, std::nullopt, 128}, DATE()),
  });

  testAggregations({data}, {"c0"}, {"arbitrary(c1)"}, {expectedResult});

  // Global aggregation.
  auto plan = PlanBuilder()
                  .values({data})
                  .singleAggregation({}, {"arbitrary(c1)"})
                  .planNode();

  auto result = readSingleValue(plan);
  ASSERT_TRUE(!result.isNull());
  ASSERT_EQ(result.kind(), TypeKind::INTEGER);

  auto date = result.value<int32_t>();
  ASSERT_EQ(date, 125);

  testAggregations({data}, {}, {"arbitrary(c2)"}, "SELECT null");
}

TEST_F(ArbitraryTest, interval) {
  auto data = makeRowVector({
      // Grouping key.
      makeFlatVector<int64_t>({1, 1, 2, 2, 3, 3, 4, 4}),
      // Input values: constant within groups.
      makeNullableFlatVector<int64_t>(
          {125, 125, 126, 126, std::nullopt, std::nullopt, std::nullopt, 128},
          INTERVAL_DAY_TIME()),
      makeConstant<Timestamp>(std::nullopt, 8),
  });

  auto expectedResult = makeRowVector({
      makeFlatVector<int64_t>({1, 2, 3, 4}),
      makeNullableFlatVector<int64_t>(
          {125, 126, std::nullopt, 128}, INTERVAL_DAY_TIME()),
  });

  testAggregations({data}, {"c0"}, {"arbitrary(c1)"}, {expectedResult});

  // Global aggregation.
  auto plan = PlanBuilder()
                  .values({data})
                  .singleAggregation({}, {"arbitrary(c1)"})
                  .planNode();

  auto interval = readSingleValue(plan);
  ASSERT_EQ(interval.value<int64_t>(), 125);

  testAggregations({data}, {}, {"arbitrary(c2)"}, "SELECT null");
}

TEST_F(ArbitraryTest, longDecimal) {
  auto data = makeRowVector({// Grouping key.
                             makeFlatVector<int64_t>({1, 1, 2, 2, 3, 3, 4, 4}),
                             makeNullableFlatVector<int128_t>(
                                 {HugeInt::build(10, 100),
                                  HugeInt::build(10, 100),
                                  HugeInt::build(10, 200),
                                  HugeInt::build(10, 200),
                                  std::nullopt,
                                  std::nullopt,
                                  std::nullopt,
                                  HugeInt::build(10, 400)},
                                 DECIMAL(38, 8))});

  auto expectedResult = makeRowVector({
      makeFlatVector<int64_t>({1, 2, 3, 4}),
      makeNullableFlatVector<int128_t>(
          {HugeInt::build(10, 100),
           HugeInt::build(10, 200),
           std::nullopt,
           HugeInt::build(10, 400)},
          DECIMAL(38, 8)),
  });

  testAggregations({data}, {"c0"}, {"arbitrary(c1)"}, {expectedResult});
}

TEST_F(ArbitraryTest, shortDecimal) {
  auto data = makeRowVector({// Grouping key.
                             makeFlatVector<int64_t>({1, 1, 2, 2, 3, 3, 4, 4}),
                             makeNullableFlatVector<int64_t>(
                                 {10000000000000000,
                                  10000000000000000,
                                  20000000000000000,
                                  20000000000000000,
                                  std::nullopt,
                                  std::nullopt,
                                  std::nullopt,
                                  40000000000000000},
                                 DECIMAL(15, 2))});

  auto expectedResult = makeRowVector({
      makeFlatVector<int64_t>({1, 2, 3, 4}),
      makeNullableFlatVector<int64_t>(
          {10000000000000000,
           20000000000000000,
           std::nullopt,
           40000000000000000},
          DECIMAL(15, 2)),
  });

  testAggregations({data}, {"c0"}, {"arbitrary(c1)"}, {expectedResult});
}

class ArbitraryWindowTest : public WindowTestBase {};

TEST_F(ArbitraryWindowTest, basic) {
  auto data = makeRowVector(
      {makeFlatVector<int64_t>({1, 2, 3, 4, 5}),
       makeArrayVector<double>({{1.0}, {2.0}, {3.0}, {4.0}, {5.0}}),
       makeFlatVector<bool>({false, false, false, false, false})});

  auto expected = makeRowVector(
      {makeFlatVector<int64_t>({1, 2, 3, 4, 5}),
       makeArrayVector<double>({{1.0}, {2.0}, {3.0}, {4.0}, {5.0}}),
       makeFlatVector<bool>({false, false, false, false, false}),
       makeFlatVector<int64_t>({1, 1, 1, 1, 1})});
  window::test::WindowTestBase::testWindowFunction(
      {data},
      "arbitrary(c0)",
      "partition by c2 order by c0",
      "range between unbounded preceding and current row",
      expected);

  expected = makeRowVector(
      {makeFlatVector<int64_t>({1, 2, 3, 4, 5}),
       makeArrayVector<double>({{1.0}, {2.0}, {3.0}, {4.0}, {5.0}}),
       makeFlatVector<bool>({false, false, false, false, false}),
       makeArrayVector<double>({{1.0}, {1.0}, {1.0}, {1.0}, {1.0}})});
  window::test::WindowTestBase::testWindowFunction(
      {data},
      "arbitrary(c1)",
      "partition by c2 order by c0",
      "range between unbounded preceding and current row",
      expected);
}

TEST_F(ArbitraryTest, spilling) {
  auto data = makeRowVector(
      {makeFlatVector<float>({0.1, 0.2, 0.3, 0.4, 0.5, 0.6}),
       makeNullableFlatVector<int64_t>({1, 2, 3, 4, 5, 6})});
  auto expected = makeRowVector(
      {makeNullableFlatVector<int64_t>({1, 2, 3, 4, 5, 6}),
       makeNullableFlatVector<float>({0.1, 0.2, 0.3, 0.4, 0.5, 0.6})});

  auto plan = PlanBuilder()
                  .values({data})
                  .singleAggregation({"c1"}, {"arbitrary(c0)"})
                  .planNode();

  std::shared_ptr<TempDirectoryPath> spillDirectory;
  AssertQueryBuilder builder(plan);

  exec::TestScopedSpillInjection scopedSpillInjection(100);
  spillDirectory = exec::test::TempDirectoryPath::create();
  builder.spillDirectory(spillDirectory->getPath())
      .config(core::QueryConfig::kSpillEnabled, "true")
      .config(core::QueryConfig::kAggregationSpillEnabled, "true")
      .config(core::QueryConfig::kSpillNumPartitionBits, "0");

  auto result = builder.maxDrivers(2).copyResults(pool_.get());
  ::facebook::velox::test::assertEqualVectors(expected, result);
}

TEST_F(ArbitraryTest, clusteredInput) {
  constexpr int kSize = 1000;
  for (int batchRows : {kSize, 13}) {
    std::vector<RowVectorPtr> data;
    for (int i = 0; i < kSize; i += batchRows) {
      auto size = std::min(batchRows, kSize - i);
      data.push_back(makeRowVector({
          makeFlatVector<int64_t>(size, [&](auto j) { return (i + j) / 17; }),
          makeFlatVector<std::string>(
              size, [&](auto j) { return std::to_string(i + j); }),
          makeFlatVector<bool>(size, [&](auto j) { return (i + j) % 11 == 0; }),
      }));
    }
    createDuckDbTable(data);
    for (bool mask : {false, true}) {
      auto builder = PlanBuilder().values(data);
      std::string expected;
      if (mask) {
        builder.partialStreamingAggregation({"c0"}, {"arbitrary(c1)"}, {"c2"});
        expected = "select c0, first(c1) filter (where c2) from tmp group by 1";
      } else {
        builder.partialStreamingAggregation({"c0"}, {"arbitrary(c1)"});
        expected = "select c0, first(c1) from tmp group by 1";
      }
      auto plan = builder.finalAggregation().planNode();
      for (bool eagerFlush : {false, true}) {
        SCOPED_TRACE(fmt::format(
            "mask={} batchRows={} eagerFlush={}", mask, batchRows, eagerFlush));
        AssertQueryBuilder(plan, duckDbQueryRunner_)
            .config(core::QueryConfig::kPreferredOutputBatchRows, batchRows)
            .config(
                core::QueryConfig::kStreamingAggregationEagerFlush, eagerFlush)
            .assertResults(expected);
      }
    }
  }
}

} // namespace
} // namespace facebook::velox::aggregate::test
