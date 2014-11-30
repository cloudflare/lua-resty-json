#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#include <stdint.h>
#include <string>
#include <fstream>
#include "../ljson_parser.h"

#ifdef DEBUG
    #define ASSERT(c) if (!(c))\
        { fprintf(stderr, "%s:%d Assert: %s\n", __FILE__, __LINE__, #c); abort(); }
#else
    #define ASSERT(c) ((void)0)
#endif

//////////////////////////////////////////////////////////////////////
//
// JsonDumper is for dumpping obj_t into a human-readable json format
//
//////////////////////////////////////////////////////////////////////
//
class JsonDumper {
public:
    JsonDumper();
    ~JsonDumper() { free_buf(); }

    void dump(const obj_t* obj);
    const char* get_buf() { return _buf; }
    void free_buf();

private:
    void dump_primitive(const obj_t*);
    int dump_str(const obj_t*, bool dryrun=false);
    void dump_hashtab(const obj_t*);
    void dump_array(const obj_t*);
    void dump_obj(const obj_t*);

    void resize(uint32_t min_remain_sz);
    void output_char(char);
    void append_str(const char* str, uint32_t str_len);

    int get_utf8_codepoint(const char* utf8_seq, int len, int& seq_len);

private:
    char* _buf;
    uint32_t _buf_len;
    uint32_t _content_len;
};

//////////////////////////////////////////////////////////////////////
//
//  TestSpecIter is class to iterate test-spec file. Test-spec file
// is in this format:
//
//       input : <the-input-json>
//       output : <the-output>
//       ...
//       input : <the-input-json>
//       output : <the-output>
//
//////////////////////////////////////////////////////////////////////
//
class TestSpecIter {
public:
    TestSpecIter(const char* file);
    ~TestSpecIter();

    // return false if error occur or hit the end of the input file.
    bool get_spec(std::string& input, std::string& output, int& linenum);

    // Return true iff error occurs.
    bool err_occur(std::string& errmsg);

private:
    bool get_line(std::string& result, const char* leading_banner,
                  char banner_delimiter, bool banner_2_space = true);

    std::string::iterator first_non_space(std::string::iterator start,
                                          std::string::iterator end) const;

    void format_err_msg(const char* fmt, ...)
        __attribute__((format(printf, 2, 3))) ;

private:
    static const char* _input_banner;
    static const char* _output_banner;
    static const char _banner_delimiter;

    std::ifstream _input_file;
    std::string _err_msg;
    bool _err_occ;
    int _cur_linenum;
};

#endif
