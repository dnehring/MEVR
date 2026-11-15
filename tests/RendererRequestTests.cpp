// SPDX-License-Identifier: MIT
#include "core/vr/TestRequest.hpp"
#include <cstdio>
int RunRendererRequestTests() {
    namespace r = mevr::vr::test_request;
    int failures = 0;
    struct Case { char const* input; r::FirstPersonRequest want; };
    Case const cases[] = {
        {"count=1 tag=ordinary", r::FirstPersonRequest::Unspecified},
        {"firstperson=on delay=2 tag=preview", r::FirstPersonRequest::On},
        {"FIRSTPERSON=OFF count=2", r::FirstPersonRequest::Off},
        {"firstperson=Status", r::FirstPersonRequest::Status},
        {"firstperson=toggle", r::FirstPersonRequest::Invalid},
        {"firstperson=", r::FirstPersonRequest::Invalid},
        {"firstperson=1", r::FirstPersonRequest::Invalid},
        {"firstperson on", r::FirstPersonRequest::Unspecified}
    };
    for (auto const& c : cases) if (r::ParseFirstPerson(r::Parse(c.input)) != c.want) {
        ++failures; std::printf("FAIL first-person request: %s\n", c.input);
    }
    return failures;
}
