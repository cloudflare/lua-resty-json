#include <inttypes.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdarg.h>
#include <fstream>
#include <string>

#include "test_util.h"

using namespace std;

//////////////////////////////////////////////////////////////////////
//
//          Implementation of JsonDumper
//
//////////////////////////////////////////////////////////////////////
//
void
JsonDumper::dump_primitive(const obj_t* the_obj) {
    obj_primitive_t* obj = (obj_primitive_t*)(void*)the_obj;
    char buf[128];
    int buf_size = sizeof(buf)/sizeof(buf[0]);

    obj_ty_t ot = (obj_ty_t)the_obj->obj_ty;
    int dump_len = 0;
    switch (ot) {
    case OT_INT64:
        dump_len = snprintf(buf, buf_size, "%" PRIi64, obj->int_val);
        break;

    case OT_FP:
        dump_len = snprintf(buf, buf_size, "%.8f", obj->db_val);
        break;

    case OT_BOOL:
        dump_len = snprintf(buf, buf_size, "%s",
                            obj->int_val ? "true" : "false");
        break;

    case OT_NULL:
        dump_len = snprintf(buf, buf_size, "null");
        break;

    case OT_STR:
        dump_str(the_obj);
        return;

    default:
        dump_len = snprintf(buf, buf_size, "(unkonwn obj of ty:%d)", (int)ot);
    }

    append_str(buf, dump_len);
}

int
JsonDumper::get_utf8_codepoint(const char* utf8_seq, int len, int& seq_real_len) {
    /* step 1: determine the length of the sequence */
    unsigned char c = *utf8_seq;
    int codepoint = 0;
    seq_real_len = 0;
    if ((c & 0xf8) == 0xf0) {
        seq_real_len = 4;
        codepoint = c & 7;
    } else if ((c & 0xf0) == 0xe0) {
        seq_real_len = 3;
        codepoint = c & 0xf;
    } else if ((c & 0xe0) == 0xc0) {
        seq_real_len = 2;
        codepoint = c & 0x1f;
    } else {
        return -1;
    }

    /* Concatenate lower-6-bits of the following UTF8s */
    for (int i = 1; i < seq_real_len; i++) {
        codepoint = ((codepoint << 6) | (utf8_seq[i] & 0x3f));
    }

    return codepoint;
}

int
JsonDumper::dump_str(const obj_t* str_obj, bool dryrun) {
    obj_primitive_t* obj = (obj_primitive_t*)(void*)str_obj;

    ASSERT(obj->obj_ty == OT_STR);

    if (!dryrun) {
        int len = dump_str(str_obj, true);
        resize( _content_len + len);
    }

    char *dest = _buf + _content_len;
    int len = 0;

    /* print the opening quotion */
    len++;
    if (!dryrun) { *dest++ = '"'; }

    const char* str = obj->str_val;
    for (int i = 0, e = str_obj->str_len; i < e;) {
        char c = str[i];
        /* case 1: Print regular ASCII */
        if ((c & 0x80) == 0) {
            char esc = 0;
            switch(c) {
            case '/': esc = '/'; break;
            case '\\': esc = '\\'; break;
            case '"': esc = '"'; break;
            case '\b': esc = 'b'; break;
            case '\f': esc = 'f'; break;
            case '\r': esc = 'r'; break;
            case '\n': esc = 'n'; break;
            case '\t': esc = 't'; break;
            default: ;
            };

            len++;
            if (esc) {
                len++;
                if (!dryrun) { *dest++ = '\\'; *dest++ = esc; }
            } else if (!dryrun) {
                *dest++ = c;
            }

            i++;
            continue;
        }

        int seq_len;
        int codepoint = get_utf8_codepoint(str + i, e - i, seq_len);
        if (codepoint < 0) {
            /* case 2: Something wrong with the UTF8 sequence. In this
             *  case, we print a question-mark and move on.
             */
            len++;
            if (!dryrun) { *dest++ = '?'; }

            i++;
            continue;
        }

        i += seq_len;

        /* case 3: print utf16 surrogate pair */
        if (codepoint >= 0x10000) {
            int low = (codepoint & 0xffff) & 0x3ff;
            int high = ((codepoint & 0xffff) >> 10) & 0x3ff;

            low |= 0xdc00;
            high |= 0xd800;

            len += 12;
            if (!dryrun) {
                sprintf(dest, "\\u%04x\\u%04x", high, low);
                dest += 12;
            }
            continue;
        }

        len += 6;
        if (!dryrun) {
            sprintf(dest, "\\u%04x", codepoint);
            dest += 6;
        }
    }

    /* print the closing quotion */
    len ++;
    if (!dryrun) {
        *dest++ = '"';
        _content_len = dest - _buf;
    }

    return len;
}

void
JsonDumper::dump_array(const obj_t* obj) {
    ASSERT(obj->obj_ty == OT_ARRAY);
    obj_composite_t* array_obj = (obj_composite_t*)(void*)obj;
    obj_t* elmt_slist = array_obj->subobjs;

    int elmt_num = obj->elmt_num;
    obj_t** elmt_vect = new obj_t*[obj->elmt_num];

    int i = elmt_num - 1;
    for (; elmt_slist != 0 && i >= 0; elmt_slist = elmt_slist->next, i --) {
        elmt_vect[i] = elmt_slist;
    }

    if (elmt_slist) {
        fprintf(stderr, "array elements list seems to be corrupted\n");
        return;
    }

    output_char('[');
    for (int i = 0; i < elmt_num; i++) {
        obj_t* elmt = elmt_vect[i];
        dump_obj(elmt);
        if (i + 1 != elmt_num) {
            output_char(',');
        }
    }
    output_char(']');

    delete[] elmt_vect;
}

void
JsonDumper::dump_hashtab(const obj_t* obj) {
    ASSERT(obj->obj_ty == OT_HASHTAB);
    obj_composite_t* htab_obj = (obj_composite_t*)(void*)obj;
    obj_t* elmt_slist = htab_obj->subobjs;

    int elmt_num = obj->elmt_num;
    obj_t** elmt_vect = new obj_t*[obj->elmt_num];

    int i = elmt_num - 1;
    for (; elmt_slist != 0 && i >= 0; elmt_slist = elmt_slist->next, i --) {
        elmt_vect[i] = elmt_slist;
    }

    if (elmt_slist) {
        fprintf(stderr, "array elements list seems to be corrupted\n");
        return;
    }

    output_char('{');

    for (int i = 0; i < elmt_num; i+=2) {
        obj_t* key = elmt_vect[i];
        dump_obj(key);

        output_char(':');

        obj_t* val= elmt_vect[i+1];
        dump_obj(val);

        if (i + 2 != elmt_num) {
            output_char(',');
        }
    }
    output_char('}');

    delete[] elmt_vect;
}

void
JsonDumper::dump_obj(const obj_t* obj) {
    obj_ty_t ot = (obj_ty_t) obj->obj_ty;
    if (ot <= OT_LAST_PRIMITIVE) {
        dump_primitive(obj);
        return;
    }

    if (ot == OT_ARRAY) {
        dump_array(obj);
        return;
    }

    if (ot == OT_HASHTAB) {
        dump_hashtab(obj);
        return;
    }

    resize(128);
    int remain_sz = _buf_len - _content_len;
    snprintf(_buf + _content_len, remain_sz, "unknown obj type %d", ot);
}

void
JsonDumper::dump(const obj_t* obj) {
    if (!_buf) {
        _buf_len = 128;
        _content_len = 0;
        _buf = (char*)malloc(_buf_len);
    }

    const obj_t* outmost = obj;
    obj_ty_t ot = (obj_ty_t) obj->obj_ty;
    if (ot > OT_LAST_PRIMITIVE) {
        obj_composite_t* cobj = (obj_composite_t*)(void*)obj;
        do {
            if ((cobj = cobj->reverse_nesting_order)) {
                outmost = (obj_t*)(void*)cobj;
            } else {
                break;
            }
        } while (true);
    }

    dump_obj(outmost);
    _buf[_content_len] = '\0';
}

void
JsonDumper::output_char(char c) {
    resize(1);
    _buf[_content_len++] = c;
}

void
JsonDumper::resize(uint32_t remain_sz) {
    uint32_t min_sz = remain_sz + _content_len + 1;
    if (_buf_len < min_sz) {
        _buf_len = min_sz * 2;
        _buf = (char*) realloc(_buf, _buf_len);
    }
}

void
JsonDumper::append_str(const char* str, uint32_t str_len) {
    resize(_content_len + str_len);

    memcpy(_buf + _content_len, str, str_len + 1);
    _content_len += str_len;
}

void
JsonDumper::free_buf() {
    if (_buf)
        free((void*)_buf);

    _buf_len = _content_len = 0;
}

JsonDumper::JsonDumper() {
    _buf = 0;
    _buf_len = 0;
    _content_len = 0;
}

//////////////////////////////////////////////////////////////////////
//
//          Implementation of TestSpecIter
//
//////////////////////////////////////////////////////////////////////
//
const char* TestSpecIter::_input_banner = "input";
const char* TestSpecIter::_output_banner = "output";
const char TestSpecIter::_banner_delimiter = ':';

TestSpecIter::TestSpecIter(const char* filename):
    _err_occ(false), _cur_linenum(0) {

    _input_file.open(filename);
    if (_input_file.fail()) {
        _err_occ = true;
        _err_msg = "fail to open ";
        _err_msg += filename;
    }
}

TestSpecIter::~TestSpecIter() {
    if (_input_file.is_open())
        _input_file.close();
}

bool
TestSpecIter::get_spec(string& input, string& output, int& linenum) {
    if (_err_occ)
        return false;

    if (!get_line(input, _input_banner, _banner_delimiter))
        return false;

    linenum = _cur_linenum;
    if (!get_line(output, _output_banner, _banner_delimiter))
        return false;

    return true;
}


string::iterator
TestSpecIter::first_non_space(string::iterator start,
                              string::iterator end) const {
    for (; start != end; ++start) {
        char c = *start;
        if (c != ' ' && c != '\t') {
            return start;
        }
    }
    return end;
}

// Get the next line of the input stream, skipping empty and comment
// lines. The next line is expected to be in the format of
// "<banner> <delimiter> ...". If it is not in this format, or something
// wrong happens, false is returned; otherwise, true is retuened, and
// the "result" is set to be the next line.
//
// if "banner_2_space" is set, the banner and delimitor are replaced
// with space.
//
bool
TestSpecIter::get_line(string& result, const char* leading_banner,
                       char banner_delimiter, bool banner_2_space) {
    if (_err_occ)
        return false;

    int banner_len = strlen(leading_banner);

    do {
        if (!getline(_input_file, result))
            return false;

        _cur_linenum++;
        string::iterator iter , str_end = result.end();
        iter = first_non_space(result.begin(), str_end);

        if (iter == str_end || *iter == '#') {
            // it is either empty line or a comment line.
            continue;
        }

        // find the leading-banner
        if (result.compare(iter - result.begin(),
                           banner_len, leading_banner) != 0) {
            format_err_msg("line:%d expect to start with '%s <space>* %c'",
                           _cur_linenum, leading_banner, banner_delimiter);
            return false;
        }

        iter = first_non_space(iter + banner_len, str_end);
        if (iter == str_end || *iter != banner_delimiter) {
            format_err_msg("line:%d expect to deliminter '%c' after '%s'",
                           _cur_linenum, banner_delimiter, leading_banner);
            return false;
        }

        iter = iter + 1;
        if (banner_2_space) {
            result.replace(result.begin(), iter, iter - result.begin(), ' ');
        }

        return true;
    } while(true);

    return false;
}

bool
TestSpecIter::err_occur(std::string& errmsg) {
    if (_err_occ) {
        errmsg = _err_msg;
        return true;
    }

    return false;
}

void __attribute__((format(printf, 2, 3)))
TestSpecIter::format_err_msg(const char* fmt, ...) {
    int buflen = 1024;
    char* buf = new char[buflen];

    va_list vl;
    va_start(vl, fmt);
    vsnprintf(buf, buflen, fmt, vl);
    va_end(vl);

    _err_msg = buf;
    delete[] buf;
}
