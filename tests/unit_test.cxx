#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <string>

#include "../ljson_parser.h"
#include "test_util.h"

using namespace std;

static int fail_num = 0;
static int test_num = 0;
static bool test_spec_wrong = false;

// trim the leading and trailing space of the given string.
static void
trim_space_both_ends(string& str) {
    const char* s = str.c_str();
    int len = str.size();
    int first, last;
    for (first = 0; first < len; first++) {
        char c = s[first];
        if (c != ' ' && c != '\t')
            break;
    }

    for (last = len - 1; last >= 0; last--) {
        char c = s[last];
        if (c != ' ' && c != '\t')
            break;
    }

    if (first != len) {
        str = str.substr(first, last - first + 1);
    } else {
        str.clear();
    }
}

void
test_driver(const char* test_spec_file, const char* message,
            bool expect_fail = false) {
    fprintf(stdout, "\n\n%s \n  (test-spec:%s)\n"
                    "========================================\n",
            message, test_spec_file);

    struct json_parser* parser = jp_create();
    if (!parser) {
        fprintf(stdout, "Fail to create parser\n");
        exit(1);
    }

    // go through each testing cases
    TestSpecIter test_iter(test_spec_file);

    string input, expect_output;
    int line_num;
    while (test_iter.get_spec(input, expect_output, line_num)) {
        test_num++;

        fprintf(stdout, "Testing line:%3d ... ", line_num);
        trim_space_both_ends(expect_output);

        string real_output;

        obj_t* result = jp_parse(parser, input.c_str(), input.size());
        if (!result) {
            if (expect_fail) {
                real_output = jp_get_err(parser);
            } else {
                fprintf(stdout, "fail! %s\n", jp_get_err(parser));
                fail_num++;
                continue;
            }
        } else {
            JsonDumper dumper;
            dumper.dump(result);
            real_output = dumper.get_buf();
        }

        if (expect_output.compare(real_output) != 0) {
            fprintf(stdout, "fail!\n   >>>expect:%s\n   >>>got:%s\n",
                    expect_output.c_str(), real_output.c_str());

            fail_num++;
            continue;
        } else {
            fprintf(stdout, "succ\n");
        }
    }

    string err_msg;
    if (test_iter.err_occur(err_msg)) {
        fprintf(stdout, "fail: %s\n", err_msg.c_str());
        test_spec_wrong = true;
    }

    jp_destroy(parser);
}

int
main(int argc, char** argv) {
    test_driver("test_spec/test_token.txt", "Scaner testing cases");
    test_driver("test_spec/test_composite.txt", "Test array/hashtab");
    test_driver("test_spec/test_diagnostic.txt", "Test diagnoistic information", true);

    fprintf(stdout,
            "\nSummary\n=====================================\n Test: %d, fail :%d\n",
            test_num, fail_num);

    return (fail_num != 0 || test_spec_wrong) ? 1 : 0;
}
