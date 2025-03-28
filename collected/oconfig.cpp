/*
 * oconfig.cpp - C++14 rewrite of Collectd's oconfig.c
 * 
 * Original Collectd's oconfig.c is licensed under GPLv2.
 * This file is for demonstration purposes and must also comply 
 * with GPL if distributed.
 *
 * This version uses the "OConfigItem", "OConfigValue", etc. classes 
 * from your custom oconfig.h, adapted to parse config files similarly 
 * to the original oconfig.c logic.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <cerrno>
#include <string>
#include <vector>
#include <memory>

#include "oconfig.h" // Your custom header with OConfigItem, OConfigValue, etc.

// ---------------------------------------------------------------------
// Token definitions (similar to the original oconfig.c)
// ---------------------------------------------------------------------
enum {
    TOK_EOF = 0,
    TOK_EOL,
    TOK_BLOCK_START,    // {
    TOK_BLOCK_END,      // }
    TOK_STRING,
    TOK_COMMENT,
    TOK_ERROR
};

static const char *token_to_string(int tok)
{
    switch (tok) {
    case TOK_EOF:          return "TOK_EOF";
    case TOK_EOL:          return "TOK_EOL";
    case TOK_BLOCK_START:  return "TOK_BLOCK_START";
    case TOK_BLOCK_END:    return "TOK_BLOCK_END";
    case TOK_STRING:       return "TOK_STRING";
    case TOK_COMMENT:      return "TOK_COMMENT";
    case TOK_ERROR:        return "TOK_ERROR";
    default:               return "TOK_UNKNOWN";
    }
}

// ---------------------------------------------------------------------
// Utility: Skip all blank spaces (not including newlines).
// Return first non-space char, or EOF if no more data.
// ---------------------------------------------------------------------
static int skip_spaces(FILE *fh)
{
    int c;
    while (true) {
        c = std::fgetc(fh);
        if (c == EOF)
            return EOF;
        if (c == '\n') {
            // We'll handle newlines separately
            std::ungetc(c, fh);
            return 'n'; // signal newline detection
        }
        if (!std::isspace(static_cast<unsigned char>(c)))
            break;
    }
    return c;
}

// ---------------------------------------------------------------------
// Utility: reads next token from file
//   - line_num is updated as we go
//   - token string is placed in 'out_str'
// Returns token type (TOK_*) or negative on error
// ---------------------------------------------------------------------
static int oconfig_next_token(FILE *fh, int &line_num, std::string &out_str)
{
    out_str.clear();

    // get next non-space (but we treat newline specially)
    int c = skip_spaces(fh);
    if (c == EOF) {
        return TOK_EOF;
    }
    else if (c == 'n') {
        // That means we saw newline
        // actually read the char
        c = std::fgetc(fh);
        if (c == '\n')
            line_num++;
        return TOK_EOL;
    }

    // Check special single-char tokens: { or }
    if (c == '{') {
        return TOK_BLOCK_START;
    }
    if (c == '}') {
        return TOK_BLOCK_END;
    }
    if (c == '#') {
        // It's a comment -> read until end of line
        // We'll store the comment content in out_str for completeness
        while (true) {
            int cc = std::fgetc(fh);
            if (cc == EOF || cc == '\n')
                break;
            out_str.push_back(static_cast<char>(cc));
        }
        if (!out_str.empty() && out_str.back() == '\r') 
            out_str.pop_back();
        if (!std::feof(fh) && !std::ferror(fh))
            line_num++; // we consumed a line
        return TOK_COMMENT;
    }
    if (c == '"' || c == '\'') {
        // Quoted string
        int quote = c;
        while (true) {
            c = std::fgetc(fh);
            if (c == EOF) {
                return TOK_ERROR;
            }
            else if (c == quote) {
                // end of string
                break;
            }
            else if (c == '\n') {
                // multiline string isn't allowed in original
                // we treat it as an error or close of string
                line_num++;
                return TOK_ERROR;
            }
            else if (c == '\\') {
                // handle escape
                int esc = std::fgetc(fh);
                if (esc == EOF) {
                    return TOK_ERROR;
                }
                else if (esc == 'n') {
                    out_str.push_back('\n');
                }
                else if (esc == 't') {
                    out_str.push_back('\t');
                }
                else if (esc == 'r') {
                    out_str.push_back('\r');
                }
                else if (esc == quote) {
                    // e.g. \" inside double-quoted string
                    out_str.push_back(static_cast<char>(quote));
                }
                else {
                    // fallback
                    out_str.push_back(static_cast<char>(esc));
                }
            }
            else {
                out_str.push_back(static_cast<char>(c));
            }
        }
        return TOK_STRING;
    }

    // If none of the above, treat as unquoted string until whitespace, {, }, #, or newline
    // We'll keep reading
    out_str.push_back(static_cast<char>(c));
    while (true) {
        int peekc = std::fgetc(fh);
        if (peekc == EOF) {
            break;
        }
        if (std::isspace(static_cast<unsigned char>(peekc))) {
            // ungetc if it's a newline
            if (peekc == '\n')
                line_num++;
            // done
            break;
        }
        if (peekc == '{' || peekc == '}' || peekc == '#' || peekc == '"' || peekc == '\'') {
            // push back, so next call will parse it
            std::ungetc(peekc, fh);
            break;
        }
        // else accumulate
        out_str.push_back(static_cast<char>(peekc));
    }
    if (out_str == "{")
        return TOK_BLOCK_START;
    if (out_str == "}")
        return TOK_BLOCK_END;
    return TOK_STRING;
}

// Forward declarations
static int oconfig_parse_file_internal(FILE *fh, OConfigItem &parent, int &line_num, const std::string &filename);

// ---------------------------------------------------------------------
// parse a block: read tokens until matching TOK_BLOCK_END
//   - store children in 'parent'
// returns 0 on success, or -1 on error
// ---------------------------------------------------------------------
static int oconfig_parse_block(FILE *fh, OConfigItem &parent, int &line_num, const std::string &filename)
{
    while (true) {
        std::string token_str;
        int tok = oconfig_next_token(fh, line_num, token_str);
        if (tok == TOK_EOF) {
            std::fprintf(stderr, "%s:%d: unexpected EOF in <block>\n", filename.c_str(), line_num);
            return -1;
        }
        else if (tok == TOK_ERROR) {
            std::fprintf(stderr, "%s:%d: parse error in <block>\n", filename.c_str(), line_num);
            return -1;
        }
        else if (tok == TOK_BLOCK_END) {
            // done
            return 0;
        }
        else if (tok == TOK_EOL || tok == TOK_COMMENT) {
            // skip
            continue;
        }
        else if (tok == TOK_BLOCK_START) {
            // This is unexpected syntax: '{' can't come without a prior key
            std::fprintf(stderr, "%s:%d: unexpected '{' in <block>\n", filename.c_str(), line_num);
            return -1;
        }
        else if (tok == TOK_STRING) {
            // we have a key
            std::string key = token_str;
            // next token might be a block start or a value
            tok = oconfig_next_token(fh, line_num, token_str);
            if (tok == TOK_BLOCK_START) {
                // We have <key> { ... } block
                OConfigItem *child = parent.addChild(key);
                if (oconfig_parse_block(fh, *child, line_num, filename) != 0)
                    return -1;
            }
            else if (tok == TOK_ERROR || tok == TOK_EOF) {
                std::fprintf(stderr, "%s:%d: parse error after key '%s'\n", filename.c_str(), line_num, key.c_str());
                return -1;
            }
            else if (tok == TOK_EOL || tok == TOK_COMMENT) {
                // That means the key has no immediate value
                // (like a string line on its own) - it could be valid or not 
                // We'll allow it but no value is stored
                // do nothing
            }
            else if (tok == TOK_BLOCK_END) {
                // That means the key alone was followed by '}' => end block
                // This is a bit unusual. We'll unget it.
                std::ungetc('}', fh);
                // so next iteration sees it
            }
            else if (tok == TOK_STRING) {
                // We treat it as "key value". 
                // Store that in parent's child item.
                OConfigItem *child = parent.addChild(key);

                // Save the value we read
                OConfigValue val(token_str);
                child->addValue(val);

                // Possibly the line might have more tokens until EOL
                // gather them all
                while (true) {
                    int peek_tok;
                    std::string peek_str;
                    peek_tok = oconfig_next_token(fh, line_num, peek_str);
                    if (peek_tok == TOK_STRING) {
                        // additional values
                        OConfigValue extra_val(peek_str);
                        child->addValue(extra_val);
                    }
                    else if (peek_tok == TOK_EOL || peek_tok == TOK_COMMENT) {
                        // done with this line
                        break;
                    }
                    else if (peek_tok == TOK_BLOCK_START) {
                        // This means the line had "key value {" => child block
                        if (oconfig_parse_block(fh, *child, line_num, filename) != 0)
                            return -1;
                        break;
                    }
                    else if (peek_tok == TOK_BLOCK_END) {
                        // end of current block
                        std::ungetc('}', fh);
                        break;
                    }
                    else if (peek_tok == TOK_EOF) {
                        // End of file while reading line
                        return 0;
                    }
                    else if (peek_tok == TOK_ERROR) {
                        std::fprintf(stderr, "%s:%d: parse error in line after key '%s'\n", filename.c_str(), line_num, key.c_str());
                        return -1;
                    }
                }
            }
            else {
                // If it's not a string or block start, we put it back (if possible) or handle error
                if (tok == TOK_BLOCK_END)
                    std::ungetc('}', fh);
                // otherwise, do nothing 
            }
        }
    } // end while
}

// ---------------------------------------------------------------------
// parse the entire file handle (FH) top-level. 
//   - fill the children into 'parent'
// ---------------------------------------------------------------------
static int oconfig_parse_file_internal(FILE *fh, OConfigItem &parent, int &line_num, const std::string &filename)
{
    while (true) {
        std::string token_str;
        int tok = oconfig_next_token(fh, line_num, token_str);
        if (tok == TOK_EOF) {
            // done
            return 0;
        }
        else if (tok == TOK_EOL || tok == TOK_COMMENT) {
            // skip empty lines / comments
            continue;
        }
        else if (tok == TOK_BLOCK_END) {
            // spurious '}' at top-level => error
            std::fprintf(stderr, "%s:%d: unexpected '}' at top level\n", filename.c_str(), line_num);
            return -1;
        }
        else if (tok == TOK_BLOCK_START) {
            // spurious '{' at top-level => error
            std::fprintf(stderr, "%s:%d: unexpected '{' at top level\n", filename.c_str(), line_num);
            return -1;
        }
        else if (tok == TOK_ERROR) {
            std::fprintf(stderr, "%s:%d: parse error\n", filename.c_str(), line_num);
            return -1;
        }
        else if (tok == TOK_STRING) {
            // We got a key
            std::string key = token_str;
            // next token might be block start or value
            tok = oconfig_next_token(fh, line_num, token_str);
            if (tok == TOK_BLOCK_START) {
                // <key> { ... }
                OConfigItem *child = parent.addChild(key);
                int status = oconfig_parse_block(fh, *child, line_num, filename);
                if (status != 0)
                    return status;
            }
            else if (tok == TOK_EOF) {
                // key at the very end with no value => allowed
                return 0;
            }
            else if (tok == TOK_ERROR) {
                std::fprintf(stderr, "%s:%d: parse error after key '%s'\n", filename.c_str(), line_num, key.c_str());
                return -1;
            }
            else if (tok == TOK_EOL || tok == TOK_COMMENT) {
                // means the key is alone on the line, no value
                // we do nothing more
            }
            else if (tok == TOK_BLOCK_END) {
                // that means "key }"? => likely error
                std::fprintf(stderr, "%s:%d: unexpected '}' after key '%s'\n", filename.c_str(), line_num, key.c_str());
                std::ungetc('}', fh);
                return -1;
            }
            else if (tok == TOK_STRING) {
                // "key value"
                OConfigItem *child = parent.addChild(key);
                child->addValue(OConfigValue(token_str));

                // possibly more tokens on same line
                while (true) {
                    int peek_tok;
                    std::string peek_str;
                    peek_tok = oconfig_next_token(fh, line_num, peek_str);
                    if (peek_tok == TOK_STRING) {
                        child->addValue(OConfigValue(peek_str));
                    }
                    else if (peek_tok == TOK_BLOCK_START) {
                        // block after values => parse block
                        int st2 = oconfig_parse_block(fh, *child, line_num, filename);
                        if (st2 != 0)
                            return st2;
                        break;
                    }
                    else if (peek_tok == TOK_EOL || peek_tok == TOK_COMMENT) {
                        break;
                    }
                    else if (peek_tok == TOK_EOF) {
                        return 0;
                    }
                    else if (peek_tok == TOK_BLOCK_END) {
                        std::ungetc('}', fh);
                        break;
                    }
                    else if (peek_tok == TOK_ERROR) {
                        std::fprintf(stderr, "%s:%d: parse error after key '%s'\n", filename.c_str(), line_num, key.c_str());
                        return -1;
                    }
                }
            }
            else {
                // unexpected token 
                if (tok == TOK_BLOCK_END)
                    std::ungetc('}', fh);
                // might be an error in actual usage
            }
        }
    }
    // unreachable
    return 0;
}

// =====================================================================
// External “public” API rewriting (similar naming to original `oconfig.c`)
// =====================================================================

// ---------------------------------------------------------------------
// oconfig_free: free an entire tree. 
// In our C++ version, if you're using std::unique_ptr in OConfigItem, 
// you typically just let it go out of scope. But we offer a function 
// that you can call to emulate original usage. 
// ---------------------------------------------------------------------
static void oconfig_free(OConfigItem *item)
{
    if (!item)
        return;
    // We assume 'item' was allocated on the heap with new. 
    delete item; // This will recursively delete children (unique_ptr) 
}

// ---------------------------------------------------------------------
// Create a new item with the given key (similar to oconfig_alloc_item).
// In pure C++ approach, you might do `new OConfigItem(key)` directly.
// This function is for API parity with original code.
// ---------------------------------------------------------------------
static OConfigItem *oconfig_new_item(const char *key)
{
    if (!key) {
        return nullptr;
    }
    OConfigItem *it = new OConfigItem(std::string(key));
    return it;
}

// ---------------------------------------------------------------------
// Clone an item (deep copy). 
// We'll leverage the copy constructor in OConfigItem (or do a manual copy).
// ---------------------------------------------------------------------
static OConfigItem *oconfig_clone(const OConfigItem &src)
{
    // We can do a direct copy because in your `oconfig.h` snippet 
    // there's no explicit copy constructor, but the default should do 
    // shallow copy of vectors. So let's do a manual deep copy:
    OConfigItem *dest = new OConfigItem(src.key);
    dest->values = src.values; // vector of OConfigValue => copies 
    // children => recursively clone
    for (size_t i = 0; i < src.children.size(); ++i) {
        OConfigItem *child_copy = oconfig_clone(*src.children[i]);
        dest->children.emplace_back(child_copy);
        child_copy->parent = dest;
    }
    return dest;
}

// ---------------------------------------------------------------------
// Set or Add values to an existing item (like original oconfig_set_* / add_*).
// In your OConfigItem design, we do OConfigValue with type info.
// ---------------------------------------------------------------------
static void oconfig_set_string(OConfigItem &item, const std::string &val)
{
    item.values.clear();
    item.values.emplace_back(val);
}

static void oconfig_set_number(OConfigItem &item, double val)
{
    item.values.clear();
    item.values.emplace_back(val);
}

static void oconfig_set_boolean(OConfigItem &item, bool val)
{
    item.values.clear();
    item.values.emplace_back(val);
}

static void oconfig_add_string(OConfigItem &item, const std::string &val)
{
    item.values.emplace_back(val);
}

static void oconfig_add_number(OConfigItem &item, double val)
{
    item.values.emplace_back(val);
}

static void oconfig_add_boolean(OConfigItem &item, bool val)
{
    item.values.emplace_back(val);
}

// ---------------------------------------------------------------------
// Clone all values from one item to another
// ---------------------------------------------------------------------
static void oconfig_clone_values(OConfigItem &dest, const OConfigItem &src)
{
    dest.values = src.values;
}

// ---------------------------------------------------------------------
// Parse a file. Return 0 on success, non-zero on error
// ---------------------------------------------------------------------
static int oconfig_parse_file(const char *filename, OConfigItem &root)
{
    if (!filename) {
        std::fprintf(stderr, "oconfig_parse_file: invalid filename (NULL)\n");
        return -1;
    }
    FILE *fh = std::fopen(filename, "r");
    if (!fh) {
        std::fprintf(stderr, "oconfig_parse_file: unable to open file '%s': %s\n",
                     filename, std::strerror(errno));
        return -1;
    }

    int line_num = 1;
    int status = oconfig_parse_file_internal(fh, root, line_num, filename);
    std::fclose(fh);
    return status;
}

// ---------------------------------------------------------------------
// Parse from a string buffer. 
// We emulate what oconfig.c does with oconfig_parse_string, which sets up 
// a memory FILE via fmemopen (if available) or a custom approach. 
// For portability, we'll do a custom approach with std::tmpfile plus 
// writing to it. 
// ---------------------------------------------------------------------
static int oconfig_parse_string(const char *buffer, OConfigItem &root)
{
    if (!buffer) {
        std::fprintf(stderr, "oconfig_parse_string: buffer is NULL\n");
        return -1;
    }
    // create a temporary file in memory or on disk
    // using tmpfile() so we don't rely on non-standard fmemopen
    FILE *fh = std::tmpfile();
    if (!fh) {
        std::fprintf(stderr, "oconfig_parse_string: tmpfile() failed\n");
        return -1;
    }
    // write the buffer to the temp file
    std::fputs(buffer, fh);
    // rewind it
    std::fflush(fh);
    std::fseek(fh, 0, SEEK_SET);

    int line_num = 1;
    int status = oconfig_parse_file_internal(fh, root, line_num, "<string>");
    std::fclose(fh);
    return status;
}

// ---------------------------------------------------------------------
// Provide a small external "C++14 style" API that matches original naming
// ---------------------------------------------------------------------

// If you need these as actual extern "C" functions, you can wrap them 
// with `extern "C" { ... }`. For now, they're just standard C++ symbols:

/* 
** The following are the final “public” exported-like functions, matching 
** many of the original names from oconfig.c. 
**
** Example usage in your code:
**   OConfigItem root("root");
**   int status = oconfig_parse_file("my.conf", root);
**   if (status == 0) { 
**       // parsing success 
**   }
** 
**   // eventually, if allocated from new, free with
**   //   oconfig_free(&root);
**   // but if root is on stack, your children are unique_ptr 
**   // you might not need an explicit free, 
**   // unless you used "oconfig_new_item" for root.
*/

// clang-format off
// (We keep the same function prototypes for clarity.)

int oconfig_parse_file_cxx14(const char *filename, OConfigItem *root)
{
    if (!root)
        return -1;
    return oconfig_parse_file(filename, *root);
}

int oconfig_parse_string_cxx14(const char *buffer, OConfigItem *root)
{
    if (!root)
        return -1;
    return oconfig_parse_string(buffer, *root);
}

OConfigItem* oconfig_new_item_cxx14(const char *key)
{
    return oconfig_new_item(key);
}

void oconfig_free_cxx14(OConfigItem *item)
{
    oconfig_free(item);
}

OConfigItem* oconfig_clone_cxx14(const OConfigItem *src)
{
    if (!src)
        return nullptr;
    return oconfig_clone(*src);
}

void oconfig_set_string_cxx14(OConfigItem *item, const char *str)
{
    if (!item || !str) 
        return;
    oconfig_set_string(*item, str);
}

void oconfig_set_number_cxx14(OConfigItem *item, double val)
{
    if (!item) 
        return;
    oconfig_set_number(*item, val);
}

void oconfig_set_boolean_cxx14(OConfigItem *item, bool val)
{
    if (!item)
        return;
    oconfig_set_boolean(*item, val);
}

void oconfig_add_string_cxx14(OConfigItem *item, const char *str)
{
    if (!item || !str)
        return;
    oconfig_add_string(*item, str);
}

void oconfig_add_number_cxx14(OConfigItem *item, double val)
{
    if (!item)
        return;
    oconfig_add_number(*item, val);
}

void oconfig_add_boolean_cxx14(OConfigItem *item, bool val)
{
    if (!item)
        return;
    oconfig_add_boolean(*item, val);
}

void oconfig_clone_values_cxx14(OConfigItem *dest, const OConfigItem *src)
{
    if (!dest || !src)
        return;
    oconfig_clone_values(*dest, *src);
}

// clang-format on

#endif // end of oconfig.cpp