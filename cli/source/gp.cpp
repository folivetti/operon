// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright 2019-2021 Heal Research

#include <chrono>
#include <cmath>
#include <cstdlib>

#include <cxxopts.hpp>
#include <fmt/core.h>

#include <memory>
#include <thread>
#include <taskflow/algorithm/reduce.hpp>
#include "operon/algorithms/gp.hpp"
#include "operon/core/format.hpp"
#include "operon/core/metrics.hpp"
#include "operon/core/version.hpp"
#include "operon/core/problem.hpp"
#include "operon/interpreter/interpreter.hpp"
#include "operon/operators/creator.hpp"
#include "operon/operators/crossover.hpp"
#include "operon/operators/evaluator.hpp"
#include "operon/operators/generator.hpp"
#include "operon/operators/initializer.hpp"
#include "operon/operators/mutation.hpp"
#include "operon/operators/reinserter.hpp"
#include "operon/operators/selector.hpp"

#include "util.hpp"
#include "operator_factory.hpp"

using Operon::BroodOffspringGenerator;

auto main(int argc, char** argv) -> int
{
    cxxopts::Options opts("operon_cli", "C++ large-scale genetic programming");
    std::string symbols = "add, sub, mul, div, exp, log, square, sqrt, cbrt, sin, cos, tan, asin, acos, atan, sinh, cosh, tanh, abs, aq, ceil, floor, fmin, fmax, log1p, logabs, sqrtabs";
    opts.add_options()
        ("dataset", "Dataset file name (csv) (required)", cxxopts::value<std::string>())
        ("shuffle", "Shuffle the input data", cxxopts::value<bool>()->default_value("false"))
        ("standardize", "Standardize the training partition (zero mean, unit variance)", cxxopts::value<bool>()->default_value("false"))
        ("train", "Training range specified as start:end (required)", cxxopts::value<std::string>())
        ("test", "Test range specified as start:end", cxxopts::value<std::string>())
        ("target", "Name of the target variable (required)", cxxopts::value<std::string>())
        ("inputs", "Comma-separated list of input variables", cxxopts::value<std::string>())
        ("error-metric", "The error metric used for calculating fitness", cxxopts::value<std::string>()->default_value("r2"))
        ("population-size", "Population size", cxxopts::value<size_t>()->default_value("1000"))
        ("pool-size", "Recombination pool size (how many generated offspring per generation)", cxxopts::value<size_t>()->default_value("1000"))
        ("seed", "Random number seed", cxxopts::value<Operon::RandomGenerator::result_type>()->default_value("0"))
        ("generations", "Number of generations", cxxopts::value<size_t>()->default_value("1000"))
        ("evaluations", "Evaluation budget", cxxopts::value<size_t>()->default_value("1000000"))
        ("iterations", "Local optimization iterations", cxxopts::value<size_t>()->default_value("0"))
        ("selection-pressure", "Selection pressure", cxxopts::value<size_t>()->default_value("100"))
        ("maxlength", "Maximum length", cxxopts::value<size_t>()->default_value("50"))
        ("maxdepth", "Maximum depth", cxxopts::value<size_t>()->default_value("10"))
        ("crossover-probability", "The probability to apply crossover", cxxopts::value<Operon::Scalar>()->default_value("1.0"))
        ("crossover-internal-probability", "Crossover bias towards swapping function nodes", cxxopts::value<Operon::Scalar>()->default_value("0.9"))
        ("mutation-probability", "The probability to apply mutation", cxxopts::value<Operon::Scalar>()->default_value("0.25"))
        ("tree-creator", "Tree creator operator to initialize the population with.", cxxopts::value<std::string>()->default_value("btc"))
        ("female-selector", "Female selection operator, with optional parameters separated by : (eg, --selector tournament:5)", cxxopts::value<std::string>()->default_value("tournament"))
        ("male-selector", "Male selection operator, with optional parameters separated by : (eg, --selector tournament:5)", cxxopts::value<std::string>()->default_value("tournament"))
        ("offspring-generator", "OffspringGenerator operator, with optional parameters separated by : (eg --offspring-generator brood:10:10)", cxxopts::value<std::string>()->default_value("basic"))
        ("reinserter", "Reinsertion operator merging offspring in the recombination pool back into the population", cxxopts::value<std::string>()->default_value("keep-best"))
        ("enable-symbols", "Comma-separated list of enabled symbols ("+symbols+")", cxxopts::value<std::string>())
        ("disable-symbols", "Comma-separated list of disabled symbols ("+symbols+")", cxxopts::value<std::string>())
        ("show-primitives", "Display the primitive set used by the algorithm")
        ("threads", "Number of threads to use for parallelism", cxxopts::value<size_t>()->default_value("0"))
        ("timelimit", "Time limit after which the algorithm will terminate", cxxopts::value<size_t>()->default_value(std::to_string(std::numeric_limits<size_t>::max())))
        ("debug", "Debug mode (more information displayed)")
        ("help", "Print help")
        ("version", "Print version and program information");

    auto result = opts.parse(argc, argv);
    if (result.arguments().empty() || result.count("help") > 0) {
        fmt::print("{}\n", opts.help());
        exit(EXIT_SUCCESS);
    }

    if (result.count("version") > 0) {
        fmt::print("{}\n", Operon::Version());
        return 0;
    }

    // parse and set default values
    Operon::GeneticAlgorithmConfig config;
    config.Generations = result["generations"].as<size_t>();
    config.PopulationSize = result["population-size"].as<size_t>();
    config.PoolSize = result["pool-size"].as<size_t>();
    config.Evaluations = result["evaluations"].as<size_t>();
    config.Iterations = result["iterations"].as<size_t>();
    config.CrossoverProbability = result["crossover-probability"].as<Operon::Scalar>();
    config.MutationProbability = result["mutation-probability"].as<Operon::Scalar>();
    config.TimeLimit = result["timelimit"].as<size_t>();
    config.Seed = std::random_device {}();

    // parse remaining config options
    Operon::Range trainingRange;
    Operon::Range testRange;
    std::unique_ptr<Operon::Dataset> dataset;
    std::string target;
    bool showPrimitiveSet = false;
    auto threads = std::thread::hardware_concurrency();
    Operon::NodeType primitiveSetConfig = Operon::PrimitiveSet::Arithmetic;

    auto maxLength = result["maxlength"].as<size_t>();
    auto maxDepth = result["maxdepth"].as<size_t>();
    auto crossoverInternalProbability = result["crossover-internal-probability"].as<Operon::Scalar>();

    try {
        for (const auto& kv : result.arguments()) {
            const auto& key = kv.key();
            const auto& value = kv.value();

            if (key == "dataset") {
                dataset = std::make_unique<Operon::Dataset>(value, true);
                ENSURE(!dataset->IsView());
            }
            if (key == "seed") {
                config.Seed = kv.as<size_t>();
            }
            if (key == "train") {
                trainingRange = Operon::ParseRange(value);
            }
            if (key == "test") {
                testRange = Operon::ParseRange(value);
            }
            if (key == "target") {
                target = value;
            }
            if (key == "maxlength") {
                maxLength = kv.as<size_t>();
            }
            if (key == "maxdepth") {
                maxDepth = kv.as<size_t>();
            }
            if (key == "enable-symbols") {
                auto mask = Operon::ParsePrimitiveSetConfig(value);
                primitiveSetConfig |= mask;
            }
            if (key == "disable-symbols") {
                auto mask = ~Operon::ParsePrimitiveSetConfig(value);
                primitiveSetConfig &= mask;
            }
            if (key == "threads") {
                threads = static_cast<decltype(threads)>(kv.as<size_t>());
            }
            if (key == "show-primitives") {
                showPrimitiveSet = true;
            }
        }

        if (showPrimitiveSet) {
            Operon::PrintPrimitives(primitiveSetConfig);
            return 0;
        }

        if (!dataset) {
            throw std::runtime_error(fmt::format("{}\n{}\n", "Error: no dataset given.", opts.help()));
        }
        if (result.count("target") == 0) {
            throw std::runtime_error(fmt::format("{}\n{}\n", "Error: no target variable given.", opts.help()));
        }
        if (auto res = dataset->GetVariable(target); !res.has_value()) {
            throw std::runtime_error(fmt::format("Target variable {} does not exist in the dataset.", target));
        }
        if (result.count("train") == 0) {
            trainingRange = Operon::Range{ 0, 2 * dataset->Rows() / 3 }; // by default use 66% of the data as training
        }
        if (result.count("test") == 0) {
            // if no test range is specified, we try to infer a reasonable range based on the trainingRange
            if (trainingRange.Start() > 0) {
                testRange = Operon::Range{ 0, trainingRange.Start() };
            } else if (trainingRange.End() < dataset->Rows()) {
                testRange = Operon::Range{ trainingRange.End(), dataset->Rows() };
            } else {
                testRange = Operon::Range{ 0, 1};
            }
        }
        // validate training range
        if (trainingRange.Start() >= dataset->Rows() || trainingRange.End() > dataset->Rows()) {
            throw std::runtime_error(fmt::format("The training range {}:{} exceeds the available data range ({} rows)\n", trainingRange.Start(), trainingRange.End(), dataset->Rows()));
        }

        if (trainingRange.Start() > trainingRange.End()) {
            throw std::runtime_error(fmt::format("Invalid training range {}:{}\n", trainingRange.Start(), trainingRange.End()));
        }

        std::vector<Operon::Variable> inputs;
        if (result.count("inputs") == 0) {
            auto variables = dataset->Variables();
            std::copy_if(variables.begin(), variables.end(), std::back_inserter(inputs), [&](auto const& var) { return var.Name != target; });
        } else {
            auto str = result["inputs"].as<std::string>();
            auto tokens = Operon::Split(str, ',');

            for (auto const& tok : tokens) {
                if (auto res = dataset->GetVariable(tok); res.has_value()) {
                    inputs.push_back(res.value());
                } else {
                    throw std::runtime_error(fmt::format("Variable {} does not exist in the dataset.", tok));
                }
            }
        }

        auto problem = Operon::Problem(*dataset).Inputs(inputs).Target(target).TrainingRange(trainingRange).TestRange(testRange);
        problem.GetPrimitiveSet().SetConfig(primitiveSetConfig);

        std::unique_ptr<Operon::CreatorBase> creator;
        creator = ParseCreator(result["tree-creator"].as<std::string>(), problem.GetPrimitiveSet(), problem.InputVariables());

        auto [amin, amax] = problem.GetPrimitiveSet().FunctionArityLimits();
        Operon::UniformTreeInitializer treeInitializer(*creator);
        treeInitializer.ParameterizeDistribution(amin+1, maxLength);
        treeInitializer.SetMinDepth(1);
        treeInitializer.SetMaxDepth(1000); // NOLINT

        Operon::NormalCoefficientInitializer coeffInitializer;
        coeffInitializer.ParameterizeDistribution(Operon::Scalar{0.0}, Operon::Scalar{1.0});

        auto crossover = Operon::SubtreeCrossover { crossoverInternalProbability, maxDepth, maxLength };
        auto mutator = Operon::MultiMutation {};
        Operon::OnePointMutation<std::normal_distribution<Operon::Scalar>> onePoint;
        onePoint.ParameterizeDistribution(Operon::Scalar{0}, Operon::Scalar{1});

        auto changeVar = Operon::ChangeVariableMutation { problem.InputVariables() };
        auto changeFunc = Operon::ChangeFunctionMutation { problem.GetPrimitiveSet() };
        auto replaceSubtree = Operon::ReplaceSubtreeMutation { *creator, maxDepth, maxLength };
        auto insertSubtree = Operon::InsertSubtreeMutation { *creator, maxDepth, maxLength, problem.GetPrimitiveSet() };
        auto removeSubtree = Operon::RemoveSubtreeMutation { problem.GetPrimitiveSet() };
        mutator.Add(onePoint, 1.0);
        mutator.Add(changeVar, 1.0);
        mutator.Add(changeFunc, 1.0);
        mutator.Add(replaceSubtree, 1.0);
        mutator.Add(insertSubtree, 1.0);
        mutator.Add(removeSubtree, 1.0);

        Operon::Interpreter interpreter;
        auto evaluator = Operon::ParseEvaluator(result["error-metric"].as<std::string>(), problem, interpreter);

        evaluator->SetLocalOptimizationIterations(config.Iterations);
        evaluator->SetBudget(config.Evaluations);

        EXPECT(problem.TrainingRange().Size() > 0);

        auto comp = [](auto const& lhs, auto const& rhs) { return lhs[0] < rhs[0]; };

        auto femaleSelector = Operon::ParseSelector(result["female-selector"].as<std::string>(), comp);
        auto maleSelector = Operon::ParseSelector(result["male-selector"].as<std::string>(), comp);

        auto generator = Operon::ParseGenerator(result["offspring-generator"].as<std::string>(), *evaluator, crossover, mutator, *femaleSelector, *maleSelector);
        auto reinserter = Operon::ParseReinserter(result["reinserter"].as<std::string>(), comp);

        Operon::RandomGenerator random(config.Seed);
        if (result["shuffle"].as<bool>()) {
            problem.GetDataset().Shuffle(random);
        }
        if (result["standardize"].as<bool>()) {
            problem.StandardizeData(problem.TrainingRange());
        }

        tf::Executor executor(threads);

        auto t0 = std::chrono::high_resolution_clock::now();

        Operon::GeneticProgrammingAlgorithm gp { problem, config, treeInitializer, coeffInitializer, *generator, *reinserter };

        auto targetValues = problem.TargetValues();
        auto targetTrain = targetValues.subspan(trainingRange.Start(), trainingRange.Size());
        auto targetTest = targetValues.subspan(testRange.Start(), testRange.Size());

        // some boilerplate for reporting results
        const size_t idx { 0 };
        auto getBest = [&](Operon::Span<Operon::Individual const> pop) -> Operon::Individual {
            const auto *minElem = std::min_element(pop.begin(), pop.end(), [&](auto const& lhs, auto const& rhs) { return lhs[idx] < rhs[idx]; });
            return *minElem;
        };

        Operon::Individual best(1);
        //auto const& pop = gp.Parents();

        auto getSize = [](Operon::Individual const& ind) { return sizeof(ind) + sizeof(ind.Genotype) + sizeof(Operon::Node) * ind.Genotype.Nodes().capacity(); };

        tf::Executor exe(threads);

        auto report = [&]() {
            auto const& pop = gp.Parents();
            auto const& off = gp.Offspring();

            best = getBest(pop);

            Operon::Vector<Operon::Scalar> estimatedTrain;
            Operon::Vector<Operon::Scalar> estimatedTest;

            tf::Taskflow taskflow;

            auto evalTrain = taskflow.emplace([&]() {
                estimatedTrain = interpreter.Evaluate<Operon::Scalar>(best.Genotype, problem.GetDataset(), trainingRange);
            });

            auto evalTest = taskflow.emplace([&]() {
                estimatedTest = interpreter.Evaluate<Operon::Scalar>(best.Genotype, problem.GetDataset(), testRange);
            });

            // scale values
            Operon::Scalar a{};
            Operon::Scalar b{};
            auto linearScaling = taskflow.emplace([&]() {
                auto stats = bivariate::accumulate<double>(estimatedTrain.data(), targetTrain.data(), estimatedTrain.size());
                a = static_cast<Operon::Scalar>(stats.covariance / stats.variance_x);
                if (!std::isfinite(a)) { a = 1; }
                b = static_cast<Operon::Scalar>(stats.mean_y - a * stats.mean_x);
            });

            double r2Train{};
            double r2Test{};
            double nmseTrain{};
            double nmseTest{};
            double maeTrain{};
            double maeTest{};

            auto scaleTrain = taskflow.emplace([&]() {
                Eigen::Map<Eigen::Array<Operon::Scalar, -1, 1>> estimated(estimatedTrain.data(), estimatedTrain.size());
                estimated = estimated * a + b;
            });

            auto scaleTest = taskflow.emplace([&]() {
                Eigen::Map<Eigen::Array<Operon::Scalar, -1, 1>> estimated(estimatedTest.data(), estimatedTest.size());
                estimated = estimated * a + b;
            });

            auto calcStats = taskflow.emplace([&]() {
                r2Train = Operon::CoefficientOfDetermination<Operon::Scalar>(estimatedTrain, targetTrain);
                r2Test = Operon::CoefficientOfDetermination<Operon::Scalar>(estimatedTest, targetTest);

                nmseTrain = Operon::NormalizedMeanSquaredError<Operon::Scalar>(estimatedTrain, targetTrain);
                nmseTest = Operon::NormalizedMeanSquaredError<Operon::Scalar>(estimatedTest, targetTest);

                maeTrain = Operon::MeanAbsoluteError<Operon::Scalar>(estimatedTrain, targetTrain);
                maeTest = Operon::MeanAbsoluteError<Operon::Scalar>(estimatedTest, targetTest);
            });

            double avgLength = 0;
            double avgQuality = 0;
            double totalMemory = 0;

            EXPECT(std::all_of(pop.begin(), pop.end(), [](auto const& ind) { return ind.Genotype.Length() > 0; }));

            auto calculateLength = taskflow.transform_reduce(pop.begin(), pop.end(), avgLength, std::plus<double>{}, [](auto const& ind) { return ind.Genotype.Length(); });
            auto calculateQuality = taskflow.transform_reduce(pop.begin(), pop.end(), avgQuality, std::plus<double>{}, [idx=idx](auto const& ind) { return ind[idx]; });
            auto calculatePopMemory = taskflow.transform_reduce(pop.begin(), pop.end(), totalMemory, std::plus{}, [&](auto const& ind) { return getSize(ind); });
            auto calculateOffMemory = taskflow.transform_reduce(off.begin(), off.end(), totalMemory, std::plus{}, [&](auto const& ind) { return getSize(ind); });

            // define task graph
            linearScaling.succeed(evalTrain, evalTest);
            linearScaling.precede(scaleTrain, scaleTest);
            calcStats.succeed(scaleTrain, scaleTest);
            calcStats.precede(calculateLength, calculateQuality, calculatePopMemory, calculateOffMemory);

            exe.run(taskflow).wait();

            avgLength /= static_cast<double>(pop.size());
            avgQuality /= static_cast<double>(pop.size());

            auto t1 = std::chrono::high_resolution_clock::now();
            auto elapsed = static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count()) / 1e6;

            fmt::print("{:.4f}\t{}\t", elapsed, gp.Generation());
            fmt::print("{:.4f}\t{:.4f}\t{:.4g}\t{:.4g}\t{:.4g}\t{:.4g}\t", r2Train, r2Test, maeTrain, maeTest, nmseTrain, nmseTest);
            fmt::print("{:.4g}\t{:.1f}\t{:.3f}\t{:.3f}\t{}\t{}\t{}\t", avgQuality, avgLength, /* shape */ 0.0, /* diversity */ 0.0, evaluator->FitnessEvaluations(), evaluator->LocalEvaluations(), evaluator->TotalEvaluations());
            fmt::print("{}\t{}\n", totalMemory, config.Seed);
        };

        gp.Run(executor, random, report);
        fmt::print("{}\n", Operon::InfixFormatter::Format(best.Genotype, problem.GetDataset(), 20));
    } catch (std::exception& e) {
        throw std::runtime_error(fmt::format("{}", e.what()));
    }

    return 0;
}

