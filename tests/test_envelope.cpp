#include "doctest.h"
#include "f20/envelope.h"

using namespace f20;
using namespace std::chrono;

namespace {

// 2024-09-07 14:16:49.120 UTC
system_clock::time_point sampleTime() {
    return system_clock::time_point(seconds(1725718609)) + milliseconds(120);
}

} // namespace

TEST_CASE("envelope round-trip with snake_case keys") {
    Envelope out;
    out.command = "status";
    out.commandType = "request";
    out.data = {{"machine_status", "Ready"}};
    out.machineName = "F20";
    out.machineSn = "09A006";
    out.transactionId = "4941-20240907-141649";

    const std::string text = serialize(out);
    CHECK(text.find(R"("command_type":"request")") != std::string::npos);
    CHECK(text.find(R"("transaction_id":"4941-20240907-141649")") != std::string::npos);

    const auto in = parseEnvelope(text);
    REQUIRE(in.has_value());
    CHECK(in->command == "status");
    CHECK(in->commandType == "request");
    CHECK(in->machineSn == "09A006");
    CHECK(in->data["machine_status"] == "Ready");
}

TEST_CASE("envelope: missing data is {}, broken messages are rejected, nothing throws") {
    const auto minimal = parseEnvelope(R"({"command":"measure","command_type":"request","transaction_id":"1"})");
    REQUIRE(minimal.has_value());
    CHECK(minimal->data.is_object());
    CHECK(minimal->data.empty());
    CHECK(minimal->machineSn.empty());

    CHECK_FALSE(parseEnvelope("not json").has_value());
    CHECK_FALSE(parseEnvelope("[1,2]").has_value());
    CHECK_FALSE(parseEnvelope(R"({"command":"measure","command_type":"request"})").has_value());
    CHECK_FALSE(parseEnvelope(R"({"command":5,"command_type":"request","transaction_id":"1"})").has_value());
    CHECK_FALSE(parseEnvelope(R"({"command":"m","command_type":"request","transaction_id":"1","data":[]})").has_value());
    CHECK_FALSE(parseEnvelope(R"({"command":"m","command_type":"request","transaction_id":"1","machine_sn":7})").has_value());
}

TEST_CASE("transaction id: NNNN-yyyyMMdd-HHmmss in UTC, counter wraps at 9999") {
    CHECK(makeTransactionId(4941, sampleTime()) == "4941-20240907-141649");
    CHECK(makeTransactionId(1, sampleTime()) == "0001-20240907-141649");
    CHECK(makeTransactionId(9999, sampleTime()) == "9999-20240907-141649");
    CHECK(makeTransactionId(10000, sampleTime()) == "0001-20240907-141649");
    CHECK(isoUtc(sampleTime()) == "2024-09-07T14:16:49.120Z");
}

TEST_CASE("topics and machine_sn use the bare serial") {
    CHECK(bareSerial("F20:09A006") == "09A006");
    CHECK(bareSerial("09A006") == "09A006");
    CHECK(sendTopic("F20:09A006") == "09A006/ar/f20/send");
    CHECK(receiveTopic("SIM001") == "SIM001/ar/f20/receive");
}

TEST_CASE("result data: spec 8.2.5.2 fields, optional n/k/roughness only when solved") {
    MeasureResult result;
    result.passed = true;
    result.gof = 0.987;
    LayerResult first;
    first.layer = 1;
    first.thicknessNm = 512.3;
    LayerResult second;
    second.layer = 2;
    second.thicknessNm = 80.0;
    second.n = 1.46;
    second.roughnessNm = 0.4;
    result.layers = {first, second};

    ResultFacts facts;
    facts.resultId = "3f2a";
    facts.measuredAtUtc = "2026-10-07T08:15:30.120Z";
    facts.recipeName = "SiO2 on Si";
    facts.sampleId = "LOT42-07";
    facts.operatorName = "op-01";
    facts.baselineAgeMinutes = 12;

    const json data = serverResultData(result, facts);
    CHECK(data["result_id"] == "3f2a");
    CHECK(data["measured_at"] == "2026-10-07T08:15:30.120Z");
    CHECK(data["sample_id"] == "LOT42-07");
    CHECK(data["passed"] == true);
    CHECK(data["baseline_age_minutes"] == 12);
    CHECK(data["reanalyzed_from"] == "");
    CHECK(data["error"] == "");
    CHECK(data["layers"][0]["thickness_nm"] == 512.3);
    CHECK_FALSE(data["layers"][0].contains("n"));
    CHECK(data["layers"][1]["n"] == 1.46);
    CHECK(data["layers"][1]["roughness_nm"] == 0.4);
    CHECK_FALSE(data["layers"][1].contains("k"));

    facts.baselineAgeMinutes.reset();
    CHECK(serverResultData(result, facts)["baseline_age_minutes"].is_null());
}
