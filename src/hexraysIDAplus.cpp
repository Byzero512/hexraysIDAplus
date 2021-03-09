#include <pro.h>

#include <diskio.hpp>
#include <hexrays.hpp>
#include <ida.hpp>
#include <kernwin.hpp>
#include <lines.hpp>
#include <loader.hpp>
#include <nalt.hpp>
#include <struct.hpp>

#ifdef TEST
#include "util.hpp"
void print_text(cfunc_t* cfunc) {
    for (int i = 0; i < cfunc->sv.size(); i++) {
        qstring line = cfunc->sv[i].line;
        for (int j = 0; j < line.size(); j++) {
            if ((line[j] >= 0x20 && line[j] < 0x7f && line[j] != '(') ||
                (line[j] == '(' && line[j - 1] != '\x01')) {
                if (line[j] == 0x20)
                    msg("#");
                else if (line[j] == '!')
                    msg("\\x%02x", line[j]);
                else
                    msg("%c", line[j]);
            }
            else {
                msg("\\x%02x", line[j]);
            }
        }
        msg("\n======================================================\n");
    }
}
void print_text(qstring& line) {
    for (int j = 0; j < line.size(); j++) {
        if ((line[j] >= 0x20 && line[j] < 0x7f && line[j] != '(') ||
            (line[j] == '(' && line[j - 1] != '\x01')) {
            if (line[j] == 0x20)
                msg("#");
            else if (line[j] == '!')
                msg("\\x%02x", line[j]);
            else
                msg("%c", line[j]);
        }
        else {
            msg("\\x%02x", line[j]);
        }
    }
    msg("\n");
}
struct TestCtree : public ctree_parentee_t {
public:
    cfunc_t* cfunc;
    TestCtree(cfunc_t* cfunc = 0) : ctree_parentee_t(), cfunc(cfunc) {};
    int idaapi visit_expr(cexpr_t* e) override {
        if (cot_call == e->op) {
            qstring str;
            e->print1(&str, this->cfunc);
            print_text(str);
            msg("e->x:\n");
            qstring str1;
            cexpr_t* e1 = e->x;
            e1->print1(&str1, this->cfunc);
            print_text(str1);
            carglist_t* a = e->a;
            for (int i = 0; i < a->size(); i++) {
                qstring stri;
                msg("arg: %d\n", i + 1);
                (*a)[i].print1(&stri, this->cfunc);
                print_text(stri);
            }
            citem_t* pe = this->cfunc->body.find_parent_of(e);
            if (pe != 0) {
                qstring strp;
                ((cinsn_t*)pe)->print1(&strp, this->cfunc);
                print_text(strp);
                msg("==============================\n");
            }
        }
        return 0;
    }
};
#endif

struct LineInfo {
    char block[sizeof(ea_t) * 2];
    char caseBlock[sizeof(ea_t) * 2] = { 0 };
    char end = 0;
};
netnode* idaplusNetnode = 0;
hexdsp_t* hexdsp;
LineInfo cursorLineInfo = { 0 };
adiff_t cursorLine = -1;

#ifdef __EA64__
#define ANCHOR_STR_LEN "16"
#define CASE_ANCHOR_MARK "00000000C"
#else
#define ANCHOR_STR_LEN "8"
#define CASE_ANCHOR_MARK "C"
#endif

#define KEYWORD_ANCHOR(keyword) "\x01\x20" keyword
#define GET_ANCHOR(x) cfunc->sv[x].line.find("\x01\x28")
#define GET_CASE_ANCHOR(x) cfunc->sv[x].line.find("\x01\x28" CASE_ANCHOR_MARK)

#define GET_LEFT_BLOCK(x) cfunc->sv[x].line.find("\x01\x09{")
#define GET_RIGHT_BLOCK(x) cfunc->sv[x].line.find("\x04\x04\x01\x09}")
#define GET_CASE(x) cfunc->sv[x].line.find(KEYWORD_ANCHOR("case"))
#define GET_DEFAULT(x) cfunc->sv[x].line.find(KEYWORD_ANCHOR("default"))

#define GET_LEFT_BLOCK_INDENT(x) ((ea_t)GET_LEFT_BLOCK(x))
#define GET_RIGHT_BLOCK_INDENT(x) ((ea_t)GET_RIGHT_BLOCK(x))
#define GET_CASE_INDENT(x)                                              \
    ((ea_t)(cfunc->sv[x].line.find(KEYWORD_ANCHOR("case")) == -1        \
                ? cfunc->sv[x].line.find(KEYWORD_ANCHOR("default"))     \
                : (cfunc->sv[x].line.find(KEYWORD_ANCHOR("case")) - 2 - \
                   sizeof(ea_t) * 2) ^                                  \
                      cfunc->sv[x].line.find(KEYWORD_ANCHOR("default")) ^ -1))

#define IS_GET_FOLD_INFO 0
#define IS_SET_FOLD_INFO 1
#define SETTING_RIGHT_BLOCK 2
#define PARSE_LINE(x, parseType)                                               \
    int mark = parseType;                                                      \
    LineInfo lineInfo;                                                         \
    citem_t *cit = 0;                                                          \
    int _start = -1;                                                           \
    int _cidx = -1;                                                            \
    int blockOffset = -1;                                                      \
    int caseOffset = -1;                                                       \
    int defaultOffset = -1;                                                    \
    if (-1 != (blockOffset = GET_LEFT_BLOCK(x)) ||                             \
        (mark && -1 != (blockOffset = GET_RIGHT_BLOCK(x)) &&                   \
         (mark = SETTING_RIGHT_BLOCK)) ||                                      \
        -1 != (caseOffset = GET_CASE(x)) ||                                    \
        -1 != (defaultOffset = GET_DEFAULT(x))) {                              \
        _start = GET_ANCHOR(x) + 2;                                            \
        qsscanf(cfunc->sv[x].line.c_str() + _start, "%" ANCHOR_STR_LEN "x",    \
                &_cidx);                                                       \
        cit = cfunc->treeitems[_cidx];                                         \
        qsnprintf(lineInfo.block, sizeof(ea_t) * 2 + 1,                        \
                  "%0" ANCHOR_STR_LEN "x", cit->ea - get_imagebase());         \
        if (-1 != defaultOffset) {                                             \
            lineInfo.caseBlock[0] = '\x01';                                    \
        } else {                                                               \
            if (-1 != caseOffset) {                                            \
                _start = GET_CASE_ANCHOR(x);                                   \
            } else {                                                           \
                _start = GET_ANCHOR(x);                                        \
            }                                                                  \
            memcpy(lineInfo.caseBlock, cfunc->sv[x].line.c_str() + _start + 2, \
                   sizeof(ea_t) * 2);                                          \
        }                                                                      \
    }
#define SET_FOLD_INFO(x) PARSE_LINE(x, IS_SET_FOLD_INFO);
#define GET_FLOD_INFO(x) PARSE_LINE(x, IS_GET_FOLD_INFO);

void doFoldCode(cfunc_t* cfunc) {
    qvector<int> markline;
    int indent = -1;
    int newIndent = -1;
    for (int i = 0; i < cfunc->sv.size(); i++) {
        GET_FLOD_INFO(i);
        if (0 != cit && 0 != idaplusNetnode->hashval_long(lineInfo.block)) {
            if (0 != cursorLineInfo.block[0] &&
                !memcmp(&lineInfo, &cursorLineInfo, sizeof(LineInfo))) {
                cursorLine = i;
            }
            indent = GET_LEFT_BLOCK_INDENT(i) ^ GET_CASE_INDENT(i) ^ -1;
            int j = i + 1;
            while (j < cfunc->sv.size()) {
                newIndent = GET_RIGHT_BLOCK_INDENT(j) ^ GET_CASE_INDENT(j) ^ -1;
                if (newIndent != -1 && indent >= newIndent) {
                    break;
                }
                j++;
            }
            markline.push_back(i);
            markline.push_back(j);
            i = j - 1;
        }
    }
    int jumpLinePad = 0;
    while (markline.size()) {
        int high = markline.back();
        markline.pop_back();
        int low = markline.back();
        markline.pop_back();

        if (-1 != (indent = GET_CASE_INDENT(low))) {
            // case or default block
            for (int j = high - 1; j > low; j--) {
                if (j != low + 1) {
                    if (j < cursorLine) {
                        jumpLinePad++;
                    }
                    cfunc->sv.erase(&(cfunc->sv[j]));
                } /*else if (high != low + 2)*/ else {
                    char* buf = (char*)malloc(indent + 2 + 1);
                    memset(buf, '\x20', indent + 2);
                    buf[indent + 2] = 0;
                    cfunc->sv[j].line = buf;
                    cfunc->sv[j].line += COLSTR("...", SCOLOR_MACRO);
                    free(buf);
                }
            }
        }
        else {
            // {} block
            for (int j = high - 1; j > low; j--) {
                if (j < cursorLine) {
                    jumpLinePad++;
                }
                cfunc->sv.erase(&(cfunc->sv[j]));
            }
            int omitStrOffset = GET_ANCHOR(low);
            if (-1 != omitStrOffset) {
                cfunc->sv[low].line.insert(omitStrOffset + 2 + sizeof(ea_t) * 2,
                    "  " COLSTR("...", SCOLOR_MACRO), 9);
            }
        }
    }
    cursorLine -= jumpLinePad;
}

void reFoldCode(vdui_t* vu) {
    cfunc_t* cfunc = vu->cfunc;
    int x = vu->cpos.lnnum;
#ifdef TEST
    int y = x + 1;
    msg("LINE[%d] ", x);
    print_text(cfunc->sv[x].line);
    // ea_t indent = GET_LEFT_BLOCK_INDENT(x) ^ GET_CASE_INDENT(x) ^ -1;
    // ea_t newIndent =
    //     GET_RIGHT_BLOCK_INDENT(y) ^ GET_CASE_INDENT(y) ^ -1;
    // msg("indent: %d newIndent: %d {: %d case: %d\n", indent,
    //    newIndent, GET_LEFT_BLOCK_INDENT(x), GET_CASE_INDENT(x));
#endif
    SET_FOLD_INFO(x);
    if (cit != 0) {
        if (idaplusNetnode->hashval_long(lineInfo.block)) {
            if (SETTING_RIGHT_BLOCK != mark) {
                idaplusNetnode->hashdel(lineInfo.block);
            }
        }
        else {
            if (SETTING_RIGHT_BLOCK == mark) {
                cursorLineInfo = lineInfo;
            }
            idaplusNetnode->hashset(lineInfo.block, 1);
        }
        vu->refresh_ctext();
        if (-1 != cursorLine) {
            simpleline_place_t* place =
                (simpleline_place_t*)get_custom_viewer_place(vu->ct, false, 0,
                    0);
            place->n = cursorLine;
            jumpto(vu->ct, place, vu->cpos.x, vu->cpos.y);
            cursorLineInfo = { 0 };
            cursorLine = -1;
        }
    }
}

ssize_t idaapi myhexrays_cb_t(void* ud, hexrays_event_t event, va_list va) {
    switch (event) {
    case hxe_func_printed: {
        break;
    }
    case hxe_text_ready: {
        vdui_t* vu = va_arg(va, vdui_t*);
        cfunc_t* cfunc = vu->cfunc;
        doFoldCode(cfunc);
        break;
    }

    case hxe_keyboard: {
        vdui_t* vu = va_arg(va, vdui_t*);
        int keyCode = va_arg(va, int);
        int shift_state = va_arg(va, int);
        if ((int)'w' == keyCode || (int)'W' == keyCode) {
            reFoldCode(vu);
        }
        break;
    }

    case hxe_double_click: {
        vdui_t* vu = va_arg(va, vdui_t*);
        cfunc_t* cfunc = vu->cfunc;
        // reFoldCode(vu);
#ifdef TEST
        int x = vu->cpos.lnnum;
        msg("LINE[%d] ", x);
        print_text(cfunc->sv[x].line);
        // static int i = 0;
        // if (i > 0) {
        //     vdui_t *vu = va_arg(va, vdui_t *);
        //     test_make_struct("test_build_struct");
        //     lvar_t *plvar = &((*vu->cfunc->get_lvars())[0]);
        //     ClassRebuilder classRebuilder;
        //     tinfo_t *tif =
        //         classRebuilder.rebuild("test11", vu->cfunc, plvar);
        //     vu->set_lvar_type(plvar, *tif);
        //     classRebuilder.print();
        // }
        // i++;

#endif
        break;
    }
    }
    return 0;
}

int idaapi init(void) {
    if (!init_hexrays_plugin()) {
        return PLUGIN_SKIP;
    }

    if (!install_hexrays_callback(myhexrays_cb_t, NULL)) {
        return PLUGIN_SKIP;
    }
    else {
        idaplusNetnode = new netnode("$idaplus", 0, true);
        if (!idaplusNetnode) {
            msg("hexraysIDAplus failed to load!\n");
            return PLUGIN_SKIP;
        }
        msg("hexraysIDAplus loaded!\n");
    }
    return PLUGIN_KEEP;
}

void idaapi term() {
    remove_hexrays_callback(myhexrays_cb_t, NULL);
    return;
}

bool idaapi run(size_t arg) { return true; }

const char comment[] = "hexraysIDAplus";
const char help[] = "click line include '{' or '}' or 'case' to flod code";
const char wanted_name[] = "IDAplus";
// const char
// wanted_hotkey[] =
// "Alt-3";

plugin_t PLUGIN{ IDP_INTERFACE_VERSION, 0,   init, term, run, comment, help,
                wanted_name,           NULL };
