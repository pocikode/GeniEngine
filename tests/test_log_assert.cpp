#include "core/Assert.h"
#include "core/CrashHandler.h"
#include "core/Log.h"
#include <cstdio>
#include <doctest/doctest.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using Geni::Log;
using Geni::LogLevel;
using Geni::LogMessage;

namespace
{

// Captures log output for one test and keeps it off the console.
struct LogCapture
{
    LogCapture()
    {
        previousLevel = Log::GetLevel();
        Log::SetLevel(LogLevel::Trace);
        Log::SetConsoleEnabled(false);
        sinkId = Log::AddSink([this](const LogMessage &message) { messages.push_back(message); });
    }

    ~LogCapture()
    {
        Log::RemoveSink(sinkId);
        Log::SetConsoleEnabled(true);
        Log::SetLevel(previousLevel);
    }

    std::vector<LogMessage> messages;
    LogLevel previousLevel;
    int sinkId = 0;
};

struct AssertRecord
{
    int count = 0;
    std::string expression;
    std::string message;
    int line = 0;
};

AssertRecord g_assertRecord;

void RecordingAssertHandler(const char *expression, const char *, int line, const char *message)
{
    ++g_assertRecord.count;
    g_assertRecord.expression = expression;
    g_assertRecord.message = message;
    g_assertRecord.line = line;
}

} // namespace

TEST_SUITE("log")
{
    TEST_CASE("messages are formatted and delivered to sinks")
    {
        LogCapture capture;
        GENI_LOG_INFO("loaded %d meshes from %s", 3, "city.gltf");

        REQUIRE(capture.messages.size() == 1);
        CHECK(capture.messages[0].level == LogLevel::Info);
        CHECK(capture.messages[0].text == "loaded 3 meshes from city.gltf");
        CHECK(std::string(capture.messages[0].file) == "test_log_assert.cpp");
        CHECK(capture.messages[0].line > 0);
    }

    TEST_CASE("messages below the level are dropped")
    {
        LogCapture capture;
        Log::SetLevel(LogLevel::Warn);
        GENI_LOG_DEBUG("hidden");
        GENI_LOG_INFO("hidden");
        GENI_LOG_WARN("shown");
        GENI_LOG_ERROR("shown");

        REQUIRE(capture.messages.size() == 2);
        CHECK(capture.messages[0].level == LogLevel::Warn);
        CHECK(capture.messages[1].level == LogLevel::Error);
    }

    TEST_CASE("long messages are not truncated")
    {
        LogCapture capture;
        const std::string longText(5000, 'x');
        GENI_LOG_INFO("%s", longText.c_str());

        REQUIRE(capture.messages.size() == 1);
        CHECK(capture.messages[0].text == longText);
    }

    TEST_CASE("recent messages are kept in order")
    {
        LogCapture capture;
        const size_t before = Log::GetMessageCount();
        for (int i = 0; i < 700; ++i) // more than the ring holds
        {
            GENI_LOG_TRACE("message %d", i);
        }
        CHECK(Log::GetMessageCount() == before + 700);

        const auto recent = Log::GetRecent(3);
        REQUIRE(recent.size() == 3);
        CHECK(recent[0].text == "message 697");
        CHECK(recent[1].text == "message 698");
        CHECK(recent[2].text == "message 699");

        std::vector<LogMessage> viaTry;
        CHECK(Log::TryGetRecent(viaTry, 1));
        REQUIRE(viaTry.size() == 1);
        CHECK(viaTry[0].text == "message 699");
    }

    TEST_CASE("warnings and errors carry their source location")
    {
        LogMessage message;
        message.level = LogLevel::Error;
        message.text = "boom";
        message.file = "Mesh.cpp";
        message.line = 42;
        CHECK(Log::FormatLine(message).find("[ERROR] boom (Mesh.cpp:42)") != std::string::npos);

        message.level = LogLevel::Info;
        CHECK(Log::FormatLine(message).find("Mesh.cpp") == std::string::npos);
    }

    TEST_CASE("messages are written to the log file")
    {
        const std::string path = "geni_test_log.txt";
        {
            LogCapture capture;
            REQUIRE(Log::OpenLogFile(path));
            GENI_LOG_WARN("written to file %d", 99);
            Log::CloseLogFile();
        }

        std::ifstream file(path);
        std::stringstream contents;
        contents << file.rdbuf();
        CHECK(contents.str().find("written to file 99") != std::string::npos);
        file.close();
        std::remove(path.c_str());
    }

    TEST_CASE("opening a log file in a missing folder reports failure")
    {
        CHECK_FALSE(Log::OpenLogFile("no/such/folder/geni.log"));
    }
}

TEST_SUITE("assert")
{
    TEST_CASE("a custom handler receives the failed expression and message")
    {
        g_assertRecord = AssertRecord();
        auto previous = Geni::SetAssertHandler(RecordingAssertHandler);

        Geni::ReportAssertFailure("index < count", "Mesh.cpp", 12, "index %d, count %d", 5, 3);
        CHECK(g_assertRecord.count == 1);
        CHECK(g_assertRecord.expression == "index < count");
        CHECK(g_assertRecord.message == "index 5, count 3");
        CHECK(g_assertRecord.line == 12);

        Geni::ReportAssertFailure("pointer", "Mesh.cpp", 13);
        CHECK(g_assertRecord.count == 2);
        CHECK(g_assertRecord.message.empty());

        Geni::SetAssertHandler(previous);
    }

    TEST_CASE("GENI_ASSERT fires only when the condition is false")
    {
        g_assertRecord = AssertRecord();
        auto previous = Geni::SetAssertHandler(RecordingAssertHandler);

        int evaluations = 0;
        GENI_ASSERT(++evaluations > 0);
        GENI_ASSERT_MSG(evaluations >= 0, "never shown %d", evaluations);
        CHECK(g_assertRecord.count == 0);

#if GENI_ENABLE_ASSERTS
        CHECK(evaluations == 1);
        GENI_ASSERT(evaluations == 100);
        CHECK(g_assertRecord.count == 1);
        CHECK(g_assertRecord.expression == "evaluations == 100");

        GENI_ASSERT_MSG(evaluations == 200, "got %d", evaluations);
        CHECK(g_assertRecord.count == 2);
        CHECK(g_assertRecord.message == "got 1");
#else
        // Compiled out: the condition must not run.
        CHECK(evaluations == 0);
#endif

        Geni::SetAssertHandler(previous);
    }
}

TEST_SUITE("stack trace")
{
    TEST_CASE("a captured trace contains frames")
    {
        const std::string trace = Geni::CaptureStackTrace();
        CHECK(trace.find("#0") != std::string::npos);
        CHECK(trace.find("no frames captured") == std::string::npos);
    }
}
