#include <stdint.h>
#include <string>
#include <string.h>

#include "../ljson_parser.h"

using namespace std;

static int fail_num = 0;
static int test_num = 0;

static const char* array_syntax_err = "Array syntax error, expect ',' or ']'";
static const char* null_in_lower_case = "'null' must be in lower case";
static const char* bool_in_lower_case = "boolean value must be in lower case";
static const char* unrecog_tk = "Unrecognizable token";

class Json {
public:
    Json(const char* json, uint32_t len = 0) {
        if (len == 0)
            len = strlen(json);
        _len = len;

        _json = new char[len + 1];
        memcpy(_json, (void*)json, len);
        _json[len] = '\0';
    }

    Json(const Json& that) {
        _json = new char[that.get_len() + 1];
        uint32_t len = that.get_len();
        memcpy(_json, (void*)that.get_text(), len);
        _json[len] = '\0';
    }

    ~Json() { delete[] _json; }

    const char* get_text() const { return _json; }
    uint32_t get_len() const { return _len; }

    void print(FILE* f=stderr);
    void print_in_line(FILE* f=stderr);

private:
    char* _json;
    uint32_t _len;
};

/////////////////////////////////////////////////////////////////////////////
//
//              Unit Testing
//
/////////////////////////////////////////////////////////////////////////////
//
class VerifyToken {
public:
    VerifyToken(const Json&, int line_num);
    ~VerifyToken();

    bool Verify(int64_t int_val);
    bool Verify(double db_val);
    bool Verify(bool bool_val);
    bool VerifyNull();
    bool VerifyString(const char* str, uint32_t str_len);

    bool VerifyErrMsg(int line, int col, const char* err_msg);

private:
    void OnFailure();
    void OnSucc();
    bool CheckType(obj_ty_t expect_ty) {
        if (!_obj || _obj->obj_ty != expect_ty) {
            OnFailure();
            return false;
        }
        return true;
    }

private:
    obj_t* _obj;
    string _err_msg;
    bool _result;
};

VerifyToken::VerifyToken(const Json& json, int line_num) {
    fprintf(stdout, "Testing %3d (line:%3d) ...", ++test_num, line_num);
    _result = true;
    _obj = 0;

    struct json_parser* parser = jp_create();
    if (!parser) {
        _err_msg = "fail to call jp_create()";
        return;
    }

    obj_t* objs = jp_parse(parser, json.get_text(), json.get_len());
    if (!objs) {
        _err_msg = jp_get_err(parser);
        return;
    }

    if (objs->obj_ty != OT_ARRAY ||
        !objs || objs->elmt_num != 1 || !objs->elmt_vect ||
        objs->elmt_vect[0]->obj_ty > OT_LAST_PRIMITIVE) {
        _err_msg = "The input json dose not seems to be in the format of "
                   "array-with-single-primitive-element";
        return;
    }

    _obj = objs->elmt_vect[0];
}

VerifyToken::~VerifyToken() {
    if (_result) {
        fputs("ok\n", stdout);
        return;
    }

    fprintf(stdout, "fail (ErrMsg:%s)\n\n", _err_msg.c_str());
}

bool
VerifyToken::Verify(int64_t int_val) {
    if (!CheckType(OT_INT64) || _obj->int_val != int_val)
        return false;
    OnSucc();
    return true;
}

bool
VerifyToken::Verify(double db_val) {
    if (!CheckType(OT_FP)) {
        OnFailure();
        return false;
    }

    double dv = _obj->db_val;
    double rel_err = dv / 10000.0;
    double abs_err = 0.00000001;

    double v1 = dv - rel_err;
    double v2 = dv + rel_err;
    if (v1 > v2)
        swap(v1, v2);

    if (dv >= v1 && dv <= v2) {
        OnSucc();
        return true;
    }

    v1 = dv - abs_err;
    v2 = dv + abs_err;

    if (v1 > v2)
        swap(v1, v2);

    if (dv >= v1 && dv <= v2) {
        OnSucc();
        return true;
    }

    OnFailure();
    return false;
}

bool
VerifyToken::Verify(bool bool_val) {
    if (!CheckType(OT_BOOL)) {
        OnFailure();
        return false;
    }

    if ((bool_val && !_obj->int_val) || (!bool_val && _obj->int_val)) {
        OnFailure();
        return false;
    }
    OnSucc();
    return true;
}

bool
VerifyToken::VerifyNull() {
    if (!CheckType(OT_NULL))
        return false;

    OnSucc();
    return true;
}

bool
VerifyToken::VerifyString(const char* str, uint32_t str_len) {
    if (!CheckType(OT_STR))
        return false;

    if ((_obj->str_len != (int)str_len) ||
        memcmp(_obj->str_val, str, str_len)) {
        OnFailure();
        return false;
    }

    OnSucc();
    return true;
}

bool
VerifyToken::VerifyErrMsg(int line, int col, const char* err_msg) {
    if (_obj) {
        OnFailure();
        return false;
    }

    int buf_len = strlen(err_msg) + 256;
    char* buf = new char[buf_len];
    sprintf(buf, "(line:%d,col:%d) %s", line, col, err_msg);
    int cmp = strcmp(buf, _err_msg.c_str());
    if (cmp) {
        OnFailure();
        fprintf(stdout, "\n \tExpecting err msg: %s\n", buf);
    } else {
        OnSucc();
    }

    delete[] buf;
    return cmp == 0;
}

void
VerifyToken::OnFailure() {
    _result = false;
    fail_num ++;
}

void
VerifyToken::OnSucc() {
    _result = true;
}

template<typename T> static bool
Verify(int line, const Json& json, T val) {
    return VerifyToken(json, line).Verify(val);
}

static bool VerifyNull(int line, const Json& json) {
    return VerifyToken(json, line).VerifyNull();
}

static bool
VerifyString(int line, const Json& json, const char* match_str,
             int32_t match_str_len = -1) {
    if (match_str_len == -1) match_str_len  = strlen(match_str);
    return VerifyToken(json, line).VerifyString(match_str, match_str_len);
}

static bool
VerifyErrMsg(int line, int json_line, int json_col,
             const Json& json, const char* errmsg) {
    return VerifyToken(json, line).VerifyErrMsg(json_line, json_col, errmsg);
}

#define VERIFY(j, v)            Verify(__LINE__, j, v)
#define VERIFY_NULL(j)          VerifyNull(__LINE__, j)
#define VERIFY_STRING(j, s)     VerifyString(__LINE__, j, s)
#define VERIFY_STRING2(j, s, l) VerifyString(__LINE__, j, s, l)
#define VERIFY_ERRMSG(j, line, col, s) VerifyErrMsg(__LINE__, line, col, j, s)

static void
test_token() {
    VERIFY(Json("[ true]"), true);
    VERIFY(Json("[ false]"), false);
    VERIFY_NULL(Json("[null ]"));
    VERIFY_STRING(Json("[\"WTF\"]"), "WTF");
}

static void
test_diagnoistic() {
    VERIFY_ERRMSG("[Null]", 1, 2, null_in_lower_case);
    VERIFY_ERRMSG("[NULL]", 1, 2, null_in_lower_case);
    VERIFY_ERRMSG("[nULL]", 1, 2, null_in_lower_case);
    VERIFY_ERRMSG("[ lol]", 1, 3, unrecog_tk);
    VERIFY_ERRMSG("[   True]", 1, 5, bool_in_lower_case);
}

int
main(int argc, char** argv) {
    test_diagnoistic();
    test_token();

    fprintf(stdout, "\n%s\n Test number: %d, Failure Number %d\n",
        "===============================================================",
        test_num, fail_num);

    return fail_num ? 1 : 0;
}
