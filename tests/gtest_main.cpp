/*
 * Copyright (C) 2014 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "bionic_gtest.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <map>
#include <string>
#include <vector>

namespace bionic_gtest {

#define DEFAULT_GLOBAL_TEST_RUN_DEADLINE_IN_MS    60000
#define DEFAULT_GLOBAL_TEST_RUN_WARNLINE_IN_MS     2000

// The time each test can run before killed for the reason of timeout.
// It takes effect only with --isolate_proc option.
static int global_test_run_deadline_in_ms = DEFAULT_GLOBAL_TEST_RUN_DEADLINE_IN_MS;

// The time each test can run before be warned for too much running time.
// It takes effect only with --isolate_proc option.
static int global_test_run_warnline_in_ms = DEFAULT_GLOBAL_TEST_RUN_WARNLINE_IN_MS;

// Store deadline information for each test written using TEST_DEADLINE like macros.
static std::map<std::string, int> bionic_test_deadline_map;

// BionicSetDeadlineInfo is used globally by TEST_DEADLINE like macros.
void BionicSetDeadlineInfo(const char* testcase_name, const char* test_name,
                               int deadline_in_ms) {
  std::string name = std::string(testcase_name) + "." + std::string(test_name);
  bionic_test_deadline_map[name] = deadline_in_ms;
}

static int BionicGetDeadlineInfo(const std::string& name) {
  int result = bionic_test_deadline_map[name];
  if (result == 0) {
    result = global_test_run_deadline_in_ms;
  }
  return result;
}

enum TestResult {
  TestSuccess = 0,
  TestFailed,
  TestTimeout
};

class TestCase {
 public:
  TestCase() {}
  explicit TestCase(const char* name) : name_(name) {}

  void SetName(const std::string name) { name_ = name; }
  std::string GetName() const { return name_; }

  void AppendTest(const std::string test_name) {
    test_name_list_.push_back(test_name);
    test_result_list_.push_back(TestFailed);
    test_time_list_.push_back(0LL);
  }

  int TestNum() const { return test_name_list_.size(); }

  std::string GetTestName(int test_id) const {
    VerifyTestId(test_id);
    return name_ + "." + test_name_list_[test_id];
  }

  void SetTestResult(int test_id, TestResult success) {
    VerifyTestId(test_id);
    test_result_list_[test_id] = success;
  }

  TestResult GetTestResult(int test_id) const {
    VerifyTestId(test_id);
    return test_result_list_[test_id];
  }

  void SetTestTime(int test_id, int64_t elapsed_time) {
    VerifyTestId(test_id);
    test_time_list_[test_id] = elapsed_time;
  }

  int64_t GetTestTime(int test_id) const {
    VerifyTestId(test_id);
    return test_time_list_[test_id];
  }

  void SetElapsedTime(int64_t elapsed_time) {
    elapsed_time_ = elapsed_time;
  }

  int64_t GetElapsedTime() const {
    return elapsed_time_;
  }

 private:
  void VerifyTestId(int test_id) const {
    if(test_id < 0 && test_id >= static_cast<int>(test_name_list_.size())) {
      fprintf(stderr, "test_id %d out of range [0, %d)\n", test_id, test_name_list_.size());
      exit(1);
    }
  }

 private:
  std::string name_;
  std::vector<std::string> test_name_list_;
  std::vector<TestResult> test_result_list_;
  std::vector<int64_t> test_time_list_;
  int64_t elapsed_time_;
};

enum GTestColor {
  COLOR_DEFAULT,
  COLOR_RED,
  COLOR_GREEN,
  COLOR_YELLOW
};

static const char* GetAnsiColorCode(GTestColor color) {
  switch (color) {
    case COLOR_RED:     return "1";
    case COLOR_GREEN:   return "2";
    case COLOR_YELLOW:  return "3";
    default:            return NULL;
  };
}

static void ColoredPrintf(GTestColor color, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);

  static const bool in_color_mode = testing::GTEST_FLAG(color) != "no" &&
                                    (isatty(fileno(stdout)) != 0);
  const bool use_color = in_color_mode && (color != COLOR_DEFAULT);

  if (!use_color) {
    vprintf(fmt, args);
    va_end(args);
    return;
  }

  printf("\033[0;3%sm", GetAnsiColorCode(color));
  vprintf(fmt, args);
  printf("\033[m");  // Resets the terminal to default.
  va_end(args);
}

class PrettyUnitTestResultPrinter : public testing::EmptyTestEventListener {
 public:
  PrettyUnitTestResultPrinter() {}
  static void PrintTestName(const char * test_case, const char * test) {
    printf("%s.%s", test_case, test);
  }

  virtual void OnTestStart(const testing::TestInfo& test_info);
  virtual void OnTestPartResult(const testing::TestPartResult& result);
  virtual void OnTestEnd(const testing::TestInfo& test_info);
};

void PrettyUnitTestResultPrinter::OnTestStart(const testing::TestInfo& test_info) {
  ColoredPrintf(COLOR_GREEN,  "[ RUN      ] ");
  PrintTestName(test_info.test_case_name(), test_info.name());
  printf("\n");
  fflush(stdout);
}

// Called after an assertion failure.
void PrettyUnitTestResultPrinter::OnTestPartResult(const testing::TestPartResult& result) {
  // If the test part succeeded, we don't need to do anything.
  if (result.type() == testing::TestPartResult::kSuccess)
    return;

  // Print failure message from the assertion (e.g. expected this and got that).
  printf("%s:(%d) Failure\n", result.file_name(), result.line_number());
  printf("%s\n", result.message());
  fflush(stdout);
}

void PrettyUnitTestResultPrinter::OnTestEnd(const testing::TestInfo& test_info) {
  if (test_info.result()->Passed()) {
    ColoredPrintf(COLOR_GREEN, "[       OK ] ");
  } else {
    ColoredPrintf(COLOR_RED, "[  FAILED  ] ");
  }
  PrintTestName(test_info.test_case_name(), test_info.name());
  if (test_info.result()->Failed()) {
    const char* const type_param = test_info.type_param();
    const char* const value_param = test_info.value_param();
    if (type_param != NULL || value_param != NULL) {
      printf(", where ");
      if (type_param != NULL) {
        printf("TypeParam = %s", type_param);
        if (value_param != NULL) {
          printf(" and ");
        }
      }
      if (value_param != NULL) {
        printf("GetParam() = %s", value_param);
      }
    }
  }

  if (testing::GTEST_FLAG(print_time)) {
    printf(" (%lld ms)\n", test_info.result()->elapsed_time());
  } else {
    printf("\n");
  }
  fflush(stdout);
}

static TestResult WaitChildProc(pid_t pid, int timeout_in_ms) {
  int result;
  int exit_status;
  bool timeout_flag = false;

  if (timeout_in_ms == 0) { // No Timeout.
    while ((result = waitpid(pid, &exit_status, 0)) == -1) {
      if (errno != EINTR) {
        break;
      }
    }
  } else {
    while (true) {
      while ((result = waitpid(pid, &exit_status, WNOHANG)) == -1) {
        if (errno != EINTR) {
          break;
        }
      }
      if (result == 0) {
        if (timeout_in_ms == 0) {
          timeout_flag = true;
          break;
        }
        timespec spec;
        spec.tv_sec = 0;
        spec.tv_nsec = 1000000;
        nanosleep(&spec, NULL);
        --timeout_in_ms;
      } else {
        break;
      }
    }
  }
  TestResult test_result = TestSuccess;
  if (timeout_flag == true) {
    test_result = TestTimeout;
  } else if (result != pid || WEXITSTATUS(exit_status) != 0) {
    test_result = TestFailed;
  }
  return test_result;
}

static bool EnumerateTests(int argc, char** argv, std::vector<TestCase>& testcase_list) {
  int pipe_fd[2];
  if (pipe(pipe_fd) == -1) {
    perror("EnumerateTests");
    return false;
  }
  pid_t pid = fork();
  if (pid == -1) {
    perror("EnumerateTests");
    return false;
  } else if (pid == 0) {
    // Child process, run gtest with --gtest_list_tests option.
    close(pipe_fd[0]);
    dup2(pipe_fd[1], 1);
    char** new_argv = new char*[argc + 1];
    memcpy(new_argv, argv, sizeof(char*) * argc);
    new_argv[argc] = const_cast<char*>("--gtest_list_tests");
    int new_argc = argc + 1;
    testing::InitGoogleTest(&new_argc, new_argv);
    int result = RUN_ALL_TESTS();
    exit(result);
  } else {
    // Parent process, wait for the output.
    close(pipe_fd[1]);
    FILE* fp = fdopen(pipe_fd[0], "r");
    char buf[200];
    while (fscanf(fp, "%s", buf) == 1) {
      int len = strlen(buf);
      if (buf[len - 1] == '.') {
        buf[len - 1] = '\0';
        testcase_list.push_back(TestCase(buf));
      } else {
        testcase_list.back().AppendTest(buf);
      }
    }
    fclose(fp);
    return (WaitChildProc(pid, 0) == TestSuccess);
  }
}

static void PrintHelpInfo() {
  printf("Bionic Unit Test Options:\n"
         "  --list_testcase\n"
         "      List the name of all test cases.\n"
         "  --list_test\n"
         "      List the name of all tests.\n"
         "  --isolate_proc\n"
         "      Run each test in a separate process.\n"
         "  --run_deadline=[TIME_IN_MS]\n"
         "      Run each test in no longer than [TIME_IN_MS] time.\n"
         "      This is a global setting, which can be substituted by TEST_DEADLINE\n"
         "      like macros used in each test. Deafult deadline is 60000 ms.\n"
         "      It takes effect only when --isolate_proc is used.\n"
         "  --run_warnline=[TIME_IN_MS]\n"
         "      Test running longer than [TIME_IN_MS] will be warned.\n"
         "      Default warnline is 2000 ms.\n"
         "      It takes effect only when --isolate_proc is used.\n"
         "\n");
}

static void ListTestCase(std::vector<TestCase> &testcase_list) {
  for (size_t i = 0; i < testcase_list.size(); ++i) {
    std::cout << testcase_list[i].GetName() << std::endl;
  }
}

static void ListTest(std::vector<TestCase> &testcase_list) {
  for (size_t i = 0; i < testcase_list.size(); ++i) {
    TestCase& testcase = testcase_list[i];
    for (int j = 0; j < testcase.TestNum(); ++j) {
      std::cout << testcase.GetTestName(j) << std::endl;
    }
  }
}

static void OnTestIterationStartPrint(std::vector<TestCase>& testcase_list, int iteration,
                                      int gtest_iteration_num) {
  if (gtest_iteration_num > 1) {
    printf("\nRepeating all tests (iteration %d) . . .\n\n", iteration);
  }
  ColoredPrintf(COLOR_GREEN,  "[==========] ");

  int test_num = 0;
  for (size_t i = 0; i < testcase_list.size(); ++i) {
    test_num += testcase_list[i].TestNum();
  }
  int testcase_num = testcase_list.size();

  printf("Running %d %s from %d %s.\n",
         test_num, (test_num == 1) ? "test" : "tests",
         testcase_num, (testcase_num == 1) ? "test case" : "test cases");
  fflush(stdout);
}

static void OnTestIterationEndPrint(std::vector<TestCase>& testcase_list, int /*iteration*/) {
  int test_num, success_test_num, fail_test_num, timeout_test_num;
  std::vector<std::string> fail_test_name_list;
  std::vector<std::string> timeout_test_name_list;
  std::vector<int64_t> timeout_test_time_list;
  int testcase_num = testcase_list.size();
  int64_t elapsed_time = 0LL;

  test_num = success_test_num = fail_test_num = timeout_test_num = 0;
  for (size_t i = 0; i < testcase_list.size(); ++i) {
    TestCase& testcase = testcase_list[i];
    elapsed_time += testcase.GetElapsedTime();
    test_num += testcase.TestNum();
    for (int j = 0; j < testcase.TestNum(); ++j) {
      TestResult result = testcase.GetTestResult(j);
      if (result == TestSuccess) {
        ++success_test_num;
      } else if (result == TestFailed) {
        ++fail_test_num;
        fail_test_name_list.push_back(testcase.GetTestName(j));
      } else if (result == TestTimeout) {
        ++timeout_test_num;
        timeout_test_name_list.push_back(testcase.GetTestName(j));
        timeout_test_time_list.push_back(testcase.GetTestTime(j));
      }
    }
  }

  ColoredPrintf(COLOR_GREEN,  "[==========] ");
  printf("%d %s from %d %s ran.", test_num, (test_num == 1) ? "test" : "tests",
                                  testcase_num, (testcase_num == 1) ? "test case" : "test cases");
  if (testing::GTEST_FLAG(print_time)) {
    printf(" (%lld ms total)", elapsed_time / 1000000LL);
  }
  printf("\n");
  ColoredPrintf(COLOR_GREEN,  "[  PASSED  ] ");
  printf("%d %s.\n", success_test_num, (success_test_num == 1) ? "test" : "tests");

  if (fail_test_num > 0) {
    ColoredPrintf(COLOR_RED,  "[  FAILED  ] ");
    printf("%d %s, listed below:\n", fail_test_num, (fail_test_num == 1) ? "test" : "tests");
    for (size_t i = 0; i < fail_test_name_list.size(); ++i) {
      ColoredPrintf(COLOR_RED, "[  FAILED  ] ");
      printf("%s\n", fail_test_name_list[i].c_str());
    }
  }
  if (timeout_test_num > 0) {
    ColoredPrintf(COLOR_RED, "[ TIMEOUT  ] ");
    printf("%d %s, listed below:\n", timeout_test_num, (timeout_test_num == 1) ? "test" : "tests");
    for (size_t i = 0; i < timeout_test_name_list.size(); ++i) {
      ColoredPrintf(COLOR_RED, "[ TIMEOUT  ] ");
      printf("%s (stopped at %lld ms)\n", timeout_test_name_list[i].c_str(),
                                          timeout_test_time_list[i] / 1000000LL);
    }
  }

  if (fail_test_num > 0) {
    printf("\n%2d FAILED %s\n", fail_test_num, (fail_test_num == 1) ? "TEST" : "TESTS");
  }
  if (timeout_test_num > 0) {
    printf("%2d TIMEOUT %s\n", timeout_test_num, (timeout_test_num == 1) ? "TEST" : "TESTS");
  }
  fflush(stdout);
}

static void OnEnvironmentsSetUpPrint() {
  ColoredPrintf(COLOR_GREEN,  "[----------] ");
  printf("Global test environment set-up.\n");
  fflush(stdout);
}

static void OnEnvironmentsTearDownPrint() {
  ColoredPrintf(COLOR_GREEN,  "[----------] ");
  printf("Global test environment tear-down\n");
  fflush(stdout);
}

static void OnTestCaseStartPrint(TestCase& testcase) {
  ColoredPrintf(COLOR_GREEN, "[----------] ");
  printf("%d %s from %s\n", testcase.TestNum(),
                            (testcase.TestNum() == 1) ? "test" : "tests",
                            testcase.GetName().c_str());
  fflush(stdout);
}

static void OnTestCaseEndPrint(TestCase& testcase) {
  if (!testing::GTEST_FLAG(print_time)) {
    return;
  }
  ColoredPrintf(COLOR_GREEN, "[----------] ");
  printf("%d %s from %s (%lld ms total)\n\n", testcase.TestNum(),
                                              (testcase.TestNum() == 1) ? "test" : "tests",
                                              testcase.GetName().c_str(),
                                              testcase.GetElapsedTime() / 1000000LL);
  fflush(stdout);
}

static void OnTestTimeoutPrint(TestCase& testcase, int test_id) {
  ColoredPrintf(COLOR_RED, "[ TIMEOUT  ] ");
  printf("%s (killed as timeout at %lld ms)\n", testcase.GetTestName(test_id).c_str(),
                                                testcase.GetTestTime(test_id) / 1000000LL);
  fflush(stdout);
}

static void OnTestTimeWarnPrint(TestCase& testcase, int test_id, int warnline_in_ms) {
  ColoredPrintf(COLOR_YELLOW, "[ TIMEWARN ] ");
  printf("%s (%lld ms, exceed warnline %d ms)\n", testcase.GetTestName(test_id).c_str(),
                                                  testcase.GetTestTime(test_id) / 1000000LL,
                                                  warnline_in_ms);
  fflush(stdout);
}

static int64_t NanoTime() {
  struct timespec t;
  t.tv_sec = t.tv_nsec = 0;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return static_cast<int64_t>(t.tv_sec) * 1000000000LL + t.tv_nsec;
}

// Fork process for each test to run.
static bool RunTestInSeparateProc(int argc, char** argv, std::vector<TestCase> &testcase_list,
                                  int gtest_repeat_num) {
  // Stop default result printer, don't dump environment setup/teardown information for each test.
  testing::UnitTest::GetInstance()->listeners().Release(
                        testing::UnitTest::GetInstance()->listeners().default_result_printer());
  testing::UnitTest::GetInstance()->listeners().Append(new PrettyUnitTestResultPrinter);

  for (int iteration = 1; iteration <= gtest_repeat_num; ++iteration) {
    OnTestIterationStartPrint(testcase_list, iteration, gtest_repeat_num);
    OnEnvironmentsSetUpPrint();
    for (size_t i = 0; i < testcase_list.size(); ++i) {
      TestCase& testcase = testcase_list[i];
      OnTestCaseStartPrint(testcase);

      int64_t testcase_start_time = NanoTime();
      for (int j = 0; j < testcase.TestNum(); ++j) {
        int64_t test_start_time = NanoTime();
        std::string test_name = testcase_list[i].GetTestName(j);
        pid_t pid = fork();
        if (pid == -1) {
          perror("RunTestInSeparateProc");
        } else if (pid == 0) {
          // Child process, run the test.
          char** new_argv = new char*[argc + 1];
          memcpy(new_argv, argv, sizeof(char*) * argc);

          char* filter_arg = new char [test_name.size() + 20];
          strcpy(filter_arg, "--gtest_filter=");
          strcat(filter_arg, test_name.c_str());
          new_argv[argc] = filter_arg;

          int new_argc = argc + 1;
          testing::InitGoogleTest(&new_argc, new_argv);
          int result = RUN_ALL_TESTS();
          exit(result);
        } else {
          // Parent process, wait for child process ending.
          int timeout_in_ms = BionicGetDeadlineInfo(testcase.GetTestName(j));
          TestResult result = WaitChildProc(pid, timeout_in_ms);
          if (result == TestTimeout) {
            // Kill and wait the child process.
            kill(pid, SIGKILL);
            WaitChildProc(pid, 0);
          }
          testcase.SetTestResult(j, result);
        }
        testcase.SetTestTime(j, NanoTime() - test_start_time);

        if (testcase.GetTestResult(j) == TestTimeout) {
          OnTestTimeoutPrint(testcase, j);
        } else if (testcase.GetTestTime(j) / 1000000LL > global_test_run_warnline_in_ms) {
          OnTestTimeWarnPrint(testcase, j, global_test_run_warnline_in_ms);
        }
      }
      testcase.SetElapsedTime(NanoTime() - testcase_start_time);
      OnTestCaseEndPrint(testcase);
    }
    OnEnvironmentsTearDownPrint();
    OnTestIterationEndPrint(testcase_list, iteration);
  }
  return true;
}

// Pick options not for gtest; Return false if run error.
// Use exit_flag to indicate whether we need to run gtest flow after PickOptions.
static bool PickOptions(int* pargc, char*** pargv, bool* exit_flag) {
  int argc = *pargc;
  char** argv = *pargv;
  *exit_flag = false;
  for (int i = 0; i < argc; ++i) {
    if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      PrintHelpInfo();
      return true;
    }
  }

  // Move --gtest_filter option to last, and add "-bionic_gtest*" to disable self test.
  int gtest_filter_option_pos = 0;
  for (int i = argc - 1; i >= 1; --i) {
    if (strncmp(argv[i], "--gtest_filter=", sizeof("--gtest_filter=") - 1) == 0) {
      gtest_filter_option_pos = i;
      break;
    }
  }
  if (gtest_filter_option_pos != 0) {
    char* gtest_filter_arg = argv[gtest_filter_option_pos];
    for (int i = gtest_filter_option_pos; i < argc - 1; ++i) {
      argv[i] = argv[i + 1];
    }
    char* new_arg = new char[strlen(gtest_filter_arg) + sizeof(":-bionic_gtest*")];
    strcpy(new_arg, gtest_filter_arg);
    strcat(new_arg, ":-bionic_gtest*");
    argv[argc - 1] = new_arg;
  } else {
    char** new_argv = new char* [argc + 1];
    for (int i = 0; i < argc; ++i) {
      new_argv[i] = argv[i];
    }
    new_argv[argc++] = const_cast<char*>("--gtest_filter=-bionic_gtest*");
    *pargv = argv = new_argv;
  }

  bool list_testcase_option = false;
  bool list_test_option = false;
  bool isolate_proc_option = false;

  int run_deadline_option_len = sizeof("--run_deadline=") - 1;
  int run_warnline_option_len = sizeof("--run_warnline=") - 1;

  for (int i = 1; i < argc; ++i) {
    bool private_option = true;
    if (strcmp(argv[i], "--list_testcase") == 0) {
      list_testcase_option = true;
    } else if (strcmp(argv[i], "--list_test") == 0) {
      list_test_option = true;
    } else if (strcmp(argv[i], "--isolate_proc") == 0) {
      isolate_proc_option = true;
    } else if (strncmp(argv[i], "--run_deadline=", run_deadline_option_len) == 0) {
      global_test_run_deadline_in_ms = atoi(argv[i] + run_deadline_option_len);
      if (global_test_run_deadline_in_ms <= 0) {
        global_test_run_deadline_in_ms = INT_MAX;
      }
    } else if (strncmp(argv[i], "--run_warnline=", run_warnline_option_len) == 0) {
      global_test_run_warnline_in_ms = atoi(argv[i] + run_warnline_option_len);
      if (global_test_run_warnline_in_ms <= 0) {
        global_test_run_deadline_in_ms = INT_MAX;
      }
    } else if (strcmp(argv[i], "--bionic_gtest") == 0) {
      isolate_proc_option = true;
      // Enable "bionic_gtest*" for self test.
      argv[argc - 1] = const_cast<char*>("--gtest_filter=bionic_gtest*");
    } else {
      private_option = false;
    }

    if (private_option) {
      for (int j = i; j < argc - 1; ++j) {
        argv[j] = argv[j + 1];
      }
      --argc; --i;
    }
  }

  int gtest_repeat_num = 1;
  if (isolate_proc_option == true) {
    // Handle --gtest_repeat=[COUNT] option if we have to manage test running.
    int len = sizeof("--gtest_repeat=") - 1;
    for (int i = 1; i < argc; ++i) {
      if (strncmp(argv[i], "--gtest_repeat=", len) == 0) {
        gtest_repeat_num = atoi(argv[i] + len);
        if (gtest_repeat_num < 0) {
          fprintf(stderr, "error count for option --gtest_repeat=[COUNT]\n");
          return false;
        }
        for (int j = i; j < argc - 1; ++j) {
          argv[j] = argv[j + 1];
        }
        --argc;
        break;
      }
    }
  }

  *pargc = argc;

  if (list_testcase_option || list_test_option || isolate_proc_option) {
    *exit_flag = true;

    std::vector<TestCase> testcase_list;
    if (EnumerateTests(argc, argv, testcase_list) == false) {
      return false;
    }
    if (list_testcase_option) {
      ListTestCase(testcase_list);
    }
    if (list_test_option) {
      ListTest(testcase_list);
    }
    if (isolate_proc_option) {
      if (RunTestInSeparateProc(argc, argv, testcase_list, gtest_repeat_num) == false) {
        return false;
      }
    }
  }
  return true;
}

} // namespace bionic_gtest

int main(int argc, char** argv) {
  bool exit_flag;

  if (bionic_gtest::PickOptions(&argc, &argv, &exit_flag) == false) {
    return 1;
  }
  if (!exit_flag) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
  }
  return 0;
}

//################################################################################
// Bionic Gtest self test, run this by --bionic_gtest option.

TEST(bionic_gtest, test_success) {
  ASSERT_EQ(1, 1);
}

TEST(bionic_gtest, test_fail) {
  ASSERT_EQ(0, 1);
}

TEST(bionic_gtest, test_time_warn) {
  sleep(4);
}

TEST(bionic_gtest, test_timeout) {
  while (1) {}
}

TEST_DEADLINE(bionic_gtest, deadline_success, 1000) {
  ASSERT_EQ(1, 1);
}

TEST_DEADLINE(bionic_gtest, deadline_fail, 1000) {
  ASSERT_EQ(0, 1);
}

TEST_DEADLINE(bionic_gtest, deadline_timeout, 1000) {
  sleep(2);
}
