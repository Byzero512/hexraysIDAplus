#ifdef TEST
#include <pro.h>

#include <diskio.hpp>
#include <hexrays.hpp>
#include <kernwin.hpp>
#include <lines.hpp>
#include <loader.hpp>
#include <struct.hpp>

qstring *type2str(tinfo_t *tif) {
    qstring *type_name = new qstring;
    // char buf[0x100] = {0};
    tif->print(type_name, NULL);
    return type_name;
}
tinfo_t *str2type(char *decl) {
    tinfo_t *ptif = new tinfo_t();
    parse_decl(ptif, NULL, NULL, (const char *)decl, PT_SIL);
    return ptif;
}

struct member {
    ea_t offset = 0;
    uval_t size = 1;
    char *name = 0;
    tinfo_t *ptif = 0;
};
bool compareOffset(struct member *a1, struct member *a2) {
    return a1->offset < a2->offset;
}
// offset name type; about union?
struc_t *make_struct(char *name, qvector<member *> &pv) {
    tid_t sid = add_struc(BADADDR, name, false);
    struc_t *pstru = get_struc(sid);
    for (int i = 0; i < pv.size(); i++) {
        member *pm = pv[i];
        ea_t offset = pm->offset;  // get_struc_size(pstru);
        add_struc_member(pstru, pm->name, offset, byte_flag(), NULL, pm->size);
        set_member_tinfo(pstru, get_member(pstru, offset), 0, *pm->ptif,
                         SET_MEMTI_MAY_DESTROY);
    }
    return pstru;
}
#ifdef DEBUG
struc_t *test_make_struct(char *name) {
    tinfo_t *ptif[3];  // = str2type("__int64;");
    ptif[0] = str2type("__int64;");
    ptif[1] = str2type("char;");
    ptif[2] = str2type("int;");
    qvector<member *> pv;
    for (int i = 0; i < 3; i++) {
        member *pm = new member();
        pm->offset = 8 * i;
        pm->name = (char *)"byte" + i;
        pm->ptif = ptif[i];
        pv.push_back(pm);
    }
    struc_t *pstru = make_struct(name, pv);
    return pstru;
}
#endif

// check whether vftable or not, if is, make vftable as struct then return
// struct's name.
char *parse_vftable(ea_t ea) { return 0; }

// pattern to find, wo not care offset 0.
// just can get offset. hot to get size by cot_cast or cot_asg
// configure vtbale through cot_asg
// (ptr+n);        // cot_add
// ((char *)ptr+n); // cot_add
// (char *)(ptr+n); // cot_cast, cot_add's parent
// (char *)((char *)ptr+n); // cot_cast, cot_add's parent
// (&var+n);
// ((char *)&var+n);
// (char *)(&var+n);
// (char *)((char *)&var+n);
struct ClassRebuilder : public ctree_parentee_t {
    qvector<member *> pv;
    cfunc_t *cfunc = 0;
    ea_t ea = 0;         // cot_obj, global var
    lvar_t *plvar = 0;   // cot_var, local var
    member_t *pmem = 0;  // struct member build; cot_memref or cot_memptr
    bool is_ref = false;
    // ClassRebuilder() : ctree_visitor_t(CV_FAST){};
    ClassRebuilder(cfunc_t *cfunc = 0) : ctree_parentee_t(), cfunc(cfunc){};

    int idaapi visit_expr(cexpr_t *e) override {
        cexpr_t *le = e;
        tinfo_t *ptif = 0;
        // e => &x || x
        // x => x->m || x.m || v.m || v->m || obj.m || obj->m
        if ((ptif = this->parse_child(e)) == 0) {
            return 0;
        }

        size_t offset = 0;
        size_t size = 1;

        e = (cexpr_t *)cfunc->body.find_parent_of(e);
        // if (0 == e) {
        //     return 0;
        // }

        // e !=> &v || &obj
        // e => e+n || (cast)e || *e
        // (TODO) e => &e || func(e) ????
        if (cot_asg != e->op) {
#define parse_cast()                                  \
    if (cot_cast == e->op) {                          \
        *ptif = e->type;                              \
        e = (cexpr_t *)cfunc->body.find_parent_of(e); \
    }
#define parse_add()                                   \
    if (cot_add == e->op) {                           \
        cexpr_t *y = e->y;                            \
        uint64 n = 0;                                 \
        if (cot_num == y->op) {                       \
            n = y->n->_value;                         \
            offset = size * n;                        \
        } else {                                      \
        }                                             \
        e = (cexpr_t *)cfunc->body.find_parent_of(e); \
    }
#define parse_ptr()                                   \
    if (cot_ptr == e->op) {                           \
        e = (cexpr_t *)cfunc->body.find_parent_of(e); \
    }
#define parse_ref()                                   \
    if (cot_ref == e->op) {                           \
        e = (cexpr_t *)cfunc->body.find_parent_of(e); \
    }
            // while(cot_call!=e->op && cot_asg!=e->op){
            // may be use for circle is more easy.
            // }
            parse_cast();
            // get offset
            parse_add();
            parse_cast();
            // parse_ref();
            // parse_cast();
            // parse_ptr();
            size = this->get_element_size(ptif);
            while (0 != e && cot_asg != e->op && cot_call != e->op) {
                e = (cexpr_t *)cfunc->body.find_parent_of(e);
                msg("e......\n");
                msg("e......\n");
            }
        }
        // e => x=y
        // parse right
        if (cot_asg == e->op) {
            e = e->y;
            if (cot_cast == e->op) {
                e = e->x;
            }
            if (cot_ref == e->op) {
                e = e->x;
            }
            if (cot_obj == e->op) {
                // may be vtable
            }
        }

        ptif->remove_ptr_or_array();
        member *pm = new member();
        char *name = (char *)malloc(0x100);
        memset(name, 0, 0x100);
        pm->size = size;
        pm->offset = offset;
        pm->ptif = ptif;
        _snprintf(name, 0x100, "offset_0x%x", pm->offset);
        pm->name = name;
        this->pv.push_back(pm);

        return 0;
    };
    tinfo_t *rebuild(char *name, cfunc_t *cfunc, lvar_t *plvar = 0, ea_t ea = 0,
                     member_t *pmem = 0) {
        this->pv.clear();
        this->cfunc = cfunc;
        this->plvar = plvar;
        this->ea = ea;
        this->pmem = pmem;
        this->is_ref = false;
        this->apply_to(&this->cfunc->body, NULL);
        std::sort(pv.begin(), pv.end(), compareOffset);
        struc_t *pst = make_struct(name, this->pv);
        tinfo_t *ptif = 0;
        if (0 != pst) {
            ptif=new tinfo_t();
            get_type(pst->id, ptif, GUESSED_DATA);
            if (this->is_ref == false) {
                ptif->create_ptr(*ptif, 0);
            }
        }
        return ptif;
    };

    tinfo_t *parse_child(cexpr_t *e) {
        tinfo_t *ptif = new tinfo_t();
        bool is_ref = false;
        if (cot_ref == e->op) {
            e = e->x;
            is_ref = true;
        }
        if (cot_var == e->op) {
            if (this->plvar != &(*cfunc->get_lvars())[e->v.idx]) {
                return 0;
            }
            *ptif = this->plvar->type();
        } else if (cot_memptr == e->op || cot_memref == e->op) {
            qvector<uint32> qoff;
            while (cot_var != e->op && cot_obj != e->op) {
                qoff.push_back(e->m);
                e = e->x;
            }
            *ptif = e->type;
            while (ptif->is_ptr()) {
                ptif->remove_ptr_or_array();
            }
            member_t *pmem = 0;
            while (qoff.size() > 0) {
                uint32 off = qoff.back();
                qoff.pop_back();
                qstring *st_name = type2str(ptif);
                tid_t tid = get_struc_id(st_name->c_str());
                struc_t *pst = get_struc(tid);
                pmem = get_member(pst, off);
                delete st_name;
            }
            if (pmem != this->pmem) {
                return 0;
            }
            get_member_tinfo(ptif, pmem);
            qoff.~qvector();
        } else if (cot_obj == e->op) {
            if (e->obj_ea != this->ea) {
                return 0;
            }
            get_tinfo(ptif, this->ea);
        } else {
            return 0;
        }
        this->is_ref = is_ref;
        if (this->is_ref) {
            ptif->create_ptr(*ptif, 0);
        }
        this->prune_now();
        return ptif;
    }

    // get element size to calc offset
    size_t get_element_size(tinfo_t *ptif) {
        size_t size = 1;
        tinfo_t ltif = remove_pointer(*ptif);
        if (ltif.is_ptr() || ltif.is_complex()) {
            size = ltif.get_size();
        }
        return size;
    }

    void print() {
        member *pm;
        for (int i = 0; i < pv.size(); i++) {
            pm = pv[i];
            msg("offset: %d, size: %d, ptif: %s, name: %s\n", pm->offset,
                pm->size, type2str(pm->ptif)->c_str(), pm->name);
        }
    }
};
#endif
