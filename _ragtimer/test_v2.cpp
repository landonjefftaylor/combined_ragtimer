#include "test_v2.h"

#include <sstream>
#include <algorithm>

#include <iostream>
#include <stdlib.h>
#include <sys/types.h>          /* See NOTES */
#include <sys/stat.h>
#include <fcntl.h>
#ifdef _WIN32
#include <winsock2.h>
#include <WS2tcpip.h>
#include <io.h>
#define isatty _isatty
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h> 
#include <sys/select.h>
#include <unistd.h>
#define _open open
#define _dup2 dup2
#endif
#include <string.h>
#include <stdio.h>
#include <string>
#if __cplusplus < 201103L
#else
#include <cstdint>
#endif
typedef test_v2 ivy_class;
std::ofstream __ivy_out;
std::ofstream __ivy_modelfile;
void __ivy_exit(int code){exit(code);}

class reader {
public:
    virtual int fdes() = 0;
    virtual void read() = 0;
    virtual void bind() {}
    virtual bool running() {return fdes() >= 0;}
    virtual ~reader() {}
};

class timer {
public:
    virtual int ms_delay() = 0;
    virtual void timeout(int) = 0;
    virtual ~timer() {}
};

#ifdef _WIN32
DWORD WINAPI ReaderThreadFunction( LPVOID lpParam ) 
{
    reader *cr = (reader *) lpParam;
    cr->bind();
    while (true)
        cr->read();
    return 0;
} 

DWORD WINAPI TimerThreadFunction( LPVOID lpParam ) 
{
    timer *cr = (timer *) lpParam;
    while (true) {
        int ms = cr->ms_delay();
        Sleep(ms);
        cr->timeout(ms);
    }
    return 0;
} 
#else
void * _thread_reader(void *rdr_void) {
    reader *rdr = (reader *) rdr_void;
    rdr->bind();
    while(rdr->running()) {
        rdr->read();
    }
    delete rdr;
    return 0; // just to stop warning
}

void * _thread_timer( void *tmr_void ) 
{
    timer *tmr = (timer *) tmr_void;
    while (true) {
        int ms = tmr->ms_delay();
        struct timespec ts;
        ts.tv_sec = ms/1000;
        ts.tv_nsec = (ms % 1000) * 1000000;
        nanosleep(&ts,NULL);
        tmr->timeout(ms);
    }
    return 0;
} 
#endif 

std::vector<reader *> threads;
std::vector<reader *> readers;
std::vector<timer *> timers;
bool initializing = false;

void test_v2::install_reader(reader *r) {
    readers.push_back(r);
    if (!::initializing)
        r->bind();
}

void test_v2::install_thread(reader *r) {
    #ifdef _WIN32

        DWORD dummy;
        HANDLE h = CreateThread( 
            NULL,                   // default security attributes
            0,                      // use default stack size  
            ReaderThreadFunction,   // thread function name
            r,                      // argument to thread function 
            0,                      // use default creation flags 
            &dummy);                // returns the thread identifier 
        if (h == NULL) {
            std::cerr << "failed to create thread" << std::endl;
            exit(1);
        }
        thread_ids.push_back(h);
    #else
        pthread_t thread;
        int res = pthread_create(&thread, NULL, _thread_reader, r);
        if (res) {
            std::cerr << "failed to create thread" << std::endl;
            exit(1);
        }
        thread_ids.push_back(thread);
    #endif
}      

void test_v2::install_timer(timer *r) {
    timers.push_back(r);
}

#ifdef _WIN32
    void test_v2::__lock() { WaitForSingleObject(mutex,INFINITE); }
    void test_v2::__unlock() { ReleaseMutex(mutex); }
#else
    void test_v2::__lock() { pthread_mutex_lock(&mutex); }
    void test_v2::__unlock() { pthread_mutex_unlock(&mutex); }
#endif

#include <string>
#include <vector>
#include <sstream>
#include <cstdlib>


using namespace hash_space;

inline z3::expr forall(const std::vector<z3::expr> &exprs, z3::expr const & b) {
    Z3_app *vars = new  Z3_app [exprs.size()];
    std::copy(exprs.begin(),exprs.end(),vars);
    Z3_ast r = Z3_mk_forall_const(b.ctx(), 0, exprs.size(), vars, 0, 0, b);
    b.check_error();
    delete vars;
    return z3::expr(b.ctx(), r);
}

class gen : public ivy_gen {

public:
    z3::context ctx;
    z3::solver slvr;
    z3::model model;

protected:
    gen(): slvr(ctx), model(ctx,(Z3_model)0) {}

    hash_map<std::string, z3::sort> enum_sorts;
    hash_map<Z3_sort, z3::func_decl_vector> enum_values;
    hash_map<std::string, z3::func_decl> decls_by_name;
    hash_map<Z3_symbol,int> enum_to_int;
    std::vector<Z3_symbol> sort_names;
    std::vector<Z3_sort> sorts;
    std::vector<Z3_symbol> decl_names;
    std::vector<Z3_func_decl> decls;
    std::vector<z3::expr> alits;


public:
    virtual bool generate(test_v2& obj)=0;
    virtual void execute(test_v2& obj)=0;
    virtual ~gen(){}

    z3::expr mk_apply_expr(const char *decl_name, unsigned num_args, const int *args){
        z3::func_decl decl = decls_by_name.find(decl_name)->second;
        std::vector<z3::expr> expr_args;
        unsigned arity = decl.arity();
        assert(arity == num_args);
        for(unsigned i = 0; i < arity; i ++) {
            z3::sort sort = decl.domain(i);
            expr_args.push_back(int_to_z3(sort,args[i]));
        }
        return decl(arity,&expr_args[0]);
    }

    int eval(const z3::expr &apply_expr) {
        try {
            z3::expr foo = model.eval(apply_expr,true);
            // std::cout << apply_expr << " = " << foo << std::endl;
            if (foo.is_int()) {
                assert(foo.is_numeral());
                int v;
                if (Z3_get_numeral_int(ctx,foo,&v) != Z3_TRUE) {
                    std::cerr << "integer value from Z3 too large for machine int: " << foo << std::endl;
                    assert(false);
                }
                return v;
            }
            if (foo.is_bv()) {
                assert(foo.is_numeral());
                unsigned v;
                if (Z3_get_numeral_uint(ctx,foo,&v) != Z3_TRUE) {
                    std::cerr << "bit vector value from Z3 too large for machine int: " << foo << std::endl;
                    assert(false);
                }
                return v;
            }
            assert(foo.is_app());
            if (foo.is_bool())
                return (foo.decl().decl_kind() == Z3_OP_TRUE) ? 1 : 0;
            return enum_to_int[foo.decl().name()];
        }
        catch (const z3::exception &e) {
            std::cerr << e << std::endl;
            throw e;
        }
    }

    __strlit eval_string(const z3::expr &apply_expr) {
        try {
            z3::expr foo = model.eval(apply_expr,true);
            assert(Z3_is_string(ctx,foo));
            return Z3_get_string(ctx,foo);
        }
        catch (const z3::exception &e) {
            std::cerr << e << std::endl;
            throw e;
        }
    }

    int eval_apply(const char *decl_name, unsigned num_args, const int *args) {
        z3::expr apply_expr = mk_apply_expr(decl_name,num_args,args);
        //        std::cout << "apply_expr: " << apply_expr << std::endl;
        try {
            z3::expr foo = model.eval(apply_expr,true);
            if (foo.is_bv() || foo.is_int()) {
                assert(foo.is_numeral());
                unsigned v;
                if (Z3_get_numeral_uint(ctx,foo,&v) != Z3_TRUE)
                    assert(false && "bit vector value too large for machine int");
                return v;
            }
            assert(foo.is_app());
            if (foo.is_bool())
                return (foo.decl().decl_kind() == Z3_OP_TRUE) ? 1 : 0;
            return enum_to_int[foo.decl().name()];
        }
        catch (const z3::exception &e) {
            std::cerr << e << std::endl;
            throw e;
        }
    }

    int eval_apply(const char *decl_name) {
        return eval_apply(decl_name,0,(int *)0);
    }

    int eval_apply(const char *decl_name, int arg0) {
        return eval_apply(decl_name,1,&arg0);
    }
    
    int eval_apply(const char *decl_name, int arg0, int arg1) {
        int args[2] = {arg0,arg1};
        return eval_apply(decl_name,2,args);
    }

    int eval_apply(const char *decl_name, int arg0, int arg1, int arg2) {
        int args[3] = {arg0,arg1,arg2};
        return eval_apply(decl_name,3,args);
    }

    int eval_apply(const char *decl_name, int arg0, int arg1, int arg2, int arg3) {
        int args[4] = {arg0,arg1,arg2,arg3};
        return eval_apply(decl_name,4,args);
    }

    z3::expr apply(const char *decl_name, std::vector<z3::expr> &expr_args) {
        z3::func_decl decl = decls_by_name.find(decl_name)->second;
        unsigned arity = decl.arity();
        assert(arity == expr_args.size());
        return decl(arity,&expr_args[0]);
    }

    z3::expr apply(const char *decl_name) {
        std::vector<z3::expr> a;
        return apply(decl_name,a);
    }

    z3::expr apply(const char *decl_name, z3::expr arg0) {
        std::vector<z3::expr> a;
        a.push_back(arg0);
        return apply(decl_name,a);
    }
    
    z3::expr apply(const char *decl_name, z3::expr arg0, z3::expr arg1) {
        std::vector<z3::expr> a;
        a.push_back(arg0);
        a.push_back(arg1);
        return apply(decl_name,a);
    }
    
    z3::expr apply(const char *decl_name, z3::expr arg0, z3::expr arg1, z3::expr arg2) {
        std::vector<z3::expr> a;
        a.push_back(arg0);
        a.push_back(arg1);
        a.push_back(arg2);
        return apply(decl_name,a);
    }

    z3::expr apply(const char *decl_name, z3::expr arg0, z3::expr arg1, z3::expr arg2, z3::expr arg3) {
        std::vector<z3::expr> a;
        a.push_back(arg0);
        a.push_back(arg1);
        a.push_back(arg2);
        a.push_back(arg3);
        return apply(decl_name,a);
    }

    z3::expr apply(const char *decl_name, z3::expr arg0, z3::expr arg1, z3::expr arg2, z3::expr arg3, z3::expr arg4) {
        std::vector<z3::expr> a;
        a.push_back(arg0);
        a.push_back(arg1);
        a.push_back(arg2);
        a.push_back(arg3);
        a.push_back(arg4);
        return apply(decl_name,a);
    }

    z3::expr int_to_z3(const z3::sort &range, int64_t value) {
        if (range.is_bool())
            return ctx.bool_val((bool)value);
        if (range.is_bv())
            return ctx.bv_val((int)value,range.bv_size());
        if (range.is_int())
            return ctx.int_val((int)value);
        return enum_values.find(range)->second[(int)value]();
    }

    z3::expr int_to_z3(const z3::sort &range, const std::string& value) {
        return ctx.string_val(value);
    }

    unsigned sort_card(const z3::sort &range) {
        if (range.is_bool())
            return 2;
        if (range.is_bv())
            return 1 << range.bv_size();
        if (range.is_int())
            return 1;  // bogus -- we need a good way to randomize ints
        return enum_values.find(range)->second.size();
    }

    int set(const char *decl_name, unsigned num_args, const int *args, int value) {
        z3::func_decl decl = decls_by_name.find(decl_name)->second;
        std::vector<z3::expr> expr_args;
        unsigned arity = decl.arity();
        assert(arity == num_args);
        for(unsigned i = 0; i < arity; i ++) {
            z3::sort sort = decl.domain(i);
            expr_args.push_back(int_to_z3(sort,args[i]));
        }
        z3::expr apply_expr = decl(arity,&expr_args[0]);
        z3::sort range = decl.range();
        z3::expr val_expr = int_to_z3(range,value);
        z3::expr pred = apply_expr == val_expr;
        //        std::cout << "pred: " << pred << std::endl;
        slvr.add(pred);
    }

    int set(const char *decl_name, int value) {
        return set(decl_name,0,(int *)0,value);
    }

    int set(const char *decl_name, int arg0, int value) {
        return set(decl_name,1,&arg0,value);
    }
    
    int set(const char *decl_name, int arg0, int arg1, int value) {
        int args[2] = {arg0,arg1};
        return set(decl_name,2,args,value);
    }

    int set(const char *decl_name, int arg0, int arg1, int arg2, int value) {
        int args[3] = {arg0,arg1,arg2};
        return set(decl_name,3,args,value);
    }

    void add_alit(const z3::expr &pred){
        if (__ivy_modelfile.is_open()) 
            __ivy_modelfile << "pred: " << pred << std::endl;
        std::ostringstream ss;
        ss << "alit:" << alits.size();
        z3::expr alit = ctx.bool_const(ss.str().c_str());
        if (__ivy_modelfile.is_open()) 
            __ivy_modelfile << "alit: " << alit << std::endl;
        alits.push_back(alit);
        slvr.add(!alit || pred);
    }

    void randomize(const z3::expr &apply_expr) {
        z3::sort range = apply_expr.get_sort();
//        std::cout << apply_expr << " : " << range << std::endl;
        unsigned card = sort_card(range);
        int value = rand() % card;
        z3::expr val_expr = int_to_z3(range,value);
        z3::expr pred = apply_expr == val_expr;
        add_alit(pred);
    }

    void randomize(const char *decl_name, unsigned num_args, const int *args) {
        z3::func_decl decl = decls_by_name.find(decl_name)->second;
        z3::expr apply_expr = mk_apply_expr(decl_name,num_args,args);
        z3::sort range = decl.range();
        unsigned card = sort_card(range);
        int value = rand() % card;
        z3::expr val_expr = int_to_z3(range,value);
        z3::expr pred = apply_expr == val_expr;
        add_alit(pred);
    }

    void randomize(const char *decl_name) {
        randomize(decl_name,0,(int *)0);
    }

    void randomize(const char *decl_name, int arg0) {
        randomize(decl_name,1,&arg0);
    }
    
    void randomize(const char *decl_name, int arg0, int arg1) {
        int args[2] = {arg0,arg1};
        randomize(decl_name,2,args);
    }

    void randomize(const char *decl_name, int arg0, int arg1, int arg2) {
        int args[3] = {arg0,arg1,arg2};
        randomize(decl_name,3,args);
    }

    void push(){
        slvr.push();
    }

    void pop(){
        slvr.pop();
    }

    z3::sort sort(const char *name) {
        if (std::string("bool") == name)
            return ctx.bool_sort();
        return enum_sorts.find(name)->second;
    }

    void mk_enum(const char *sort_name, unsigned num_values, char const * const * value_names) {
        z3::func_decl_vector cs(ctx), ts(ctx);
        z3::sort sort = ctx.enumeration_sort(sort_name, num_values, value_names, cs, ts);
        // can't use operator[] here because the value classes don't have nullary constructors
        enum_sorts.insert(std::pair<std::string, z3::sort>(sort_name,sort));
        enum_values.insert(std::pair<Z3_sort, z3::func_decl_vector>(sort,cs));
        sort_names.push_back(Z3_mk_string_symbol(ctx,sort_name));
        sorts.push_back(sort);
        for(unsigned i = 0; i < num_values; i++){
            Z3_symbol sym = Z3_mk_string_symbol(ctx,value_names[i]);
            decl_names.push_back(sym);
            decls.push_back(cs[i]);
            enum_to_int[sym] = i;
        }
    }

    void mk_bv(const char *sort_name, unsigned width) {
        z3::sort sort = ctx.bv_sort(width);
        // can't use operator[] here because the value classes don't have nullary constructors
        enum_sorts.insert(std::pair<std::string, z3::sort>(sort_name,sort));
    }

    void mk_int(const char *sort_name) {
        z3::sort sort = ctx.int_sort();
        // can't use operator[] here because the value classes don't have nullary constructors
        enum_sorts.insert(std::pair<std::string, z3::sort>(sort_name,sort));
    }

    void mk_string(const char *sort_name) {
        z3::sort sort = ctx.string_sort();
        // can't use operator[] here because the value classes don't have nullary constructors
        enum_sorts.insert(std::pair<std::string, z3::sort>(sort_name,sort));
    }

    void mk_sort(const char *sort_name) {
        Z3_symbol symb = Z3_mk_string_symbol(ctx,sort_name);
        z3::sort sort(ctx,Z3_mk_uninterpreted_sort(ctx, symb));
//        z3::sort sort = ctx.uninterpreted_sort(sort_name);
        // can't use operator[] here because the value classes don't have nullary constructors
        enum_sorts.insert(std::pair<std::string, z3::sort>(sort_name,sort));
        sort_names.push_back(symb);
        sorts.push_back(sort);
    }

    void mk_decl(const char *decl_name, unsigned arity, const char **domain_names, const char *range_name) {
        std::vector<z3::sort> domain;
        for (unsigned i = 0; i < arity; i++)
            domain.push_back(enum_sorts.find(domain_names[i])->second);
        std::string bool_name("Bool");
        z3::sort range = (range_name == bool_name) ? ctx.bool_sort() : enum_sorts.find(range_name)->second;   
        z3::func_decl decl = ctx.function(decl_name,arity,&domain[0],range);
        decl_names.push_back(Z3_mk_string_symbol(ctx,decl_name));
        decls.push_back(decl);
        decls_by_name.insert(std::pair<std::string, z3::func_decl>(decl_name,decl));
    }

    void mk_const(const char *const_name, const char *sort_name) {
        mk_decl(const_name,0,0,sort_name);
    }

    void add(const std::string &z3inp) {
        z3::expr fmla(ctx,Z3_parse_smtlib2_string(ctx, z3inp.c_str(), sort_names.size(), &sort_names[0], &sorts[0], decl_names.size(), &decl_names[0], &decls[0]));
        ctx.check_error();

        slvr.add(fmla);
    }

    bool solve() {
        // std::cout << alits.size();
        static bool show_model = true;
        if (__ivy_modelfile.is_open()) 
            __ivy_modelfile << "begin check:\n" << slvr << "end check:\n" << std::endl;
        while(true){
            if (__ivy_modelfile.is_open()) {
                __ivy_modelfile << "(check-sat"; 
                for (unsigned i = 0; i < alits.size(); i++)
                    __ivy_modelfile << " " << alits[i];
                __ivy_modelfile << ")" << std::endl;
            }
            z3::check_result res = slvr.check(alits.size(),&alits[0]);
            if (res != z3::unsat)
                break;
            z3::expr_vector core = slvr.unsat_core();
            if (core.size() == 0){
//                if (__ivy_modelfile.is_open()) 
//                    __ivy_modelfile << "begin unsat:\n" << slvr << "end unsat:\n" << std::endl;
                return false;
            }
            if (__ivy_modelfile.is_open()) 
                for (unsigned i = 0; i < core.size(); i++)
                    __ivy_modelfile << "core: " << core[i] << std::endl;
            unsigned idx = rand() % core.size();
            z3::expr to_delete = core[idx];
            if (__ivy_modelfile.is_open()) 
                __ivy_modelfile << "to delete: " << to_delete << std::endl;
            for (unsigned i = 0; i < alits.size(); i++)
                if (z3::eq(alits[i],to_delete)) {
                    alits[i] = alits.back();
                    alits.pop_back();
                    break;
                }
        }
        model = slvr.get_model();
        alits.clear();

        if(__ivy_modelfile.is_open()){
            __ivy_modelfile << "begin sat:\n" << slvr << "end sat:\n" << std::endl;
            __ivy_modelfile << model;
            __ivy_modelfile.flush();
        }

        return true;
    }

    int choose(int rng, const char *name){
        if (decls_by_name.find(name) == decls_by_name.end())
            return 0;
        return eval_apply(name);
    }
};

/*++
Copyright (c) Microsoft Corporation

This string hash function is borrowed from Microsoft Z3
(https://github.com/Z3Prover/z3). 

--*/


#define mix(a,b,c)              \
{                               \
  a -= b; a -= c; a ^= (c>>13); \
  b -= c; b -= a; b ^= (a<<8);  \
  c -= a; c -= b; c ^= (b>>13); \
  a -= b; a -= c; a ^= (c>>12); \
  b -= c; b -= a; b ^= (a<<16); \
  c -= a; c -= b; c ^= (b>>5);  \
  a -= b; a -= c; a ^= (c>>3);  \
  b -= c; b -= a; b ^= (a<<10); \
  c -= a; c -= b; c ^= (b>>15); \
}

#ifndef __fallthrough
#define __fallthrough
#endif

namespace hash_space {

// I'm using Bob Jenkin's hash function.
// http://burtleburtle.net/bob/hash/doobs.html
unsigned string_hash(const char * str, unsigned length, unsigned init_value) {
    register unsigned a, b, c, len;

    /* Set up the internal state */
    len = length;
    a = b = 0x9e3779b9;  /* the golden ratio; an arbitrary value */
    c = init_value;      /* the previous hash value */

    /*---------------------------------------- handle most of the key */
    while (len >= 12) {
        a += reinterpret_cast<const unsigned *>(str)[0];
        b += reinterpret_cast<const unsigned *>(str)[1];
        c += reinterpret_cast<const unsigned *>(str)[2];
        mix(a,b,c);
        str += 12; len -= 12;
    }

    /*------------------------------------- handle the last 11 bytes */
    c += length;
    switch(len) {        /* all the case statements fall through */
    case 11: 
        c+=((unsigned)str[10]<<24);
        __fallthrough;
    case 10: 
        c+=((unsigned)str[9]<<16);
        __fallthrough;
    case 9 : 
        c+=((unsigned)str[8]<<8);
        __fallthrough;
        /* the first byte of c is reserved for the length */
    case 8 : 
        b+=((unsigned)str[7]<<24);
        __fallthrough;
    case 7 : 
        b+=((unsigned)str[6]<<16);
        __fallthrough;
    case 6 : 
        b+=((unsigned)str[5]<<8);
        __fallthrough;
    case 5 : 
        b+=str[4];
        __fallthrough;
    case 4 : 
        a+=((unsigned)str[3]<<24);
        __fallthrough;
    case 3 : 
        a+=((unsigned)str[2]<<16);
        __fallthrough;
    case 2 : 
        a+=((unsigned)str[1]<<8);
        __fallthrough;
    case 1 : 
        a+=str[0];
        __fallthrough;
        /* case 0: nothing left to add */
    }
    mix(a,b,c);
    /*-------------------------------------------- report the result */
    return c;
}

}




struct ivy_value {
    int pos;
    std::string atom;
    std::vector<ivy_value> fields;
    bool is_member() const {
        return atom.size() && fields.size();
    }
};
struct deser_err {
};

struct ivy_ser {
    virtual void  set(long long) = 0;
    virtual void  set(bool) = 0;
    virtual void  setn(long long inp, int len) = 0;
    virtual void  set(const std::string &) = 0;
    virtual void  open_list(int len) = 0;
    virtual void  close_list() = 0;
    virtual void  open_list_elem() = 0;
    virtual void  close_list_elem() = 0;
    virtual void  open_struct() = 0;
    virtual void  close_struct() = 0;
    virtual void  open_field(const std::string &) = 0;
    virtual void  close_field() = 0;
    virtual void  open_tag(int, const std::string &) {throw deser_err();}
    virtual void  close_tag() {}
    virtual ~ivy_ser(){}
};
struct ivy_binary_ser : public ivy_ser {
    std::vector<char> res;
    void setn(long long inp, int len) {
        for (int i = len-1; i >= 0 ; i--)
            res.push_back((inp>>(8*i))&0xff);
    }
    void set(long long inp) {
        setn(inp,sizeof(long long));
    }
    void set(bool inp) {
        set((long long)inp);
    }
    void set(const std::string &inp) {
        for (unsigned i = 0; i < inp.size(); i++)
            res.push_back(inp[i]);
        res.push_back(0);
    }
    void open_list(int len) {
        set((long long)len);
    }
    void close_list() {}
    void open_list_elem() {}
    void close_list_elem() {}
    void open_struct() {}
    void close_struct() {}
    virtual void  open_field(const std::string &) {}
    void close_field() {}
    virtual void  open_tag(int tag, const std::string &) {
        set((long long)tag);
    }
    virtual void  close_tag() {}
};

struct ivy_deser {
    virtual void  get(long long&) = 0;
    virtual void  get(std::string &) = 0;
    virtual void  getn(long long &res, int bytes) = 0;
    virtual void  open_list() = 0;
    virtual void  close_list() = 0;
    virtual bool  open_list_elem() = 0;
    virtual void  close_list_elem() = 0;
    virtual void  open_struct() = 0;
    virtual void  close_struct() = 0;
    virtual void  open_field(const std::string &) = 0;
    virtual void  close_field() = 0;
    virtual int   open_tag(const std::vector<std::string> &) {throw deser_err();}
    virtual void  close_tag() {}
    virtual void  end() = 0;
    virtual ~ivy_deser(){}
};

struct ivy_binary_deser : public ivy_deser {
    std::vector<char> inp;
    int pos;
    std::vector<int> lenstack;
    ivy_binary_deser(const std::vector<char> &inp) : inp(inp),pos(0) {}
    virtual bool more(unsigned bytes) {return inp.size() >= pos + bytes;}
    virtual bool can_end() {return pos == inp.size();}
    void get(long long &res) {
       getn(res,8);
    }
    void getn(long long &res, int bytes) {
        if (!more(bytes))
            throw deser_err();
        res = 0;
        for (int i = 0; i < bytes; i++)
            res = (res << 8) | (((long long)inp[pos++]) & 0xff);
    }
    void get(std::string &res) {
        while (more(1) && inp[pos]) {
//            if (inp[pos] == '"')
//                throw deser_err();
            res.push_back(inp[pos++]);
        }
        if(!(more(1) && inp[pos] == 0))
            throw deser_err();
        pos++;
    }
    void open_list() {
        long long len;
        get(len);
        lenstack.push_back(len);
    }
    void close_list() {
        lenstack.pop_back();
    }
    bool open_list_elem() {
        return lenstack.back();
    }
    void close_list_elem() {
        lenstack.back()--;
    }
    void open_struct() {}
    void close_struct() {}
    virtual void  open_field(const std::string &) {}
    void close_field() {}
    int open_tag(const std::vector<std::string> &tags) {
        long long res;
        get(res);
        if (res >= tags.size())
            throw deser_err();
        return res;
    }
    void end() {
        if (!can_end())
            throw deser_err();
    }
};
struct ivy_socket_deser : public ivy_binary_deser {
      int sock;
    public:
      ivy_socket_deser(int sock, const std::vector<char> &inp)
          : ivy_binary_deser(inp), sock(sock) {}
    virtual bool more(unsigned bytes) {
        while (inp.size() < pos + bytes) {
            int oldsize = inp.size();
            int get = pos + bytes - oldsize;
            get = (get < 1024) ? 1024 : get;
            inp.resize(oldsize + get);
            int newbytes;
	    if ((newbytes = read(sock,&inp[oldsize],get)) < 0)
		 { std::cerr << "recvfrom failed\n"; exit(1); }
            inp.resize(oldsize + newbytes);
            if (newbytes == 0)
                 return false;
        }
        return true;
    }
    virtual bool can_end() {return true;}
};

struct out_of_bounds {
    std::string txt;
    int pos;
    out_of_bounds(int _idx, int pos = 0) : pos(pos){
        std::ostringstream os;
        os << "argument " << _idx+1;
        txt = os.str();
    }
    out_of_bounds(const std::string &s, int pos = 0) : txt(s), pos(pos) {}
};

template <class T> T _arg(std::vector<ivy_value> &args, unsigned idx, long long bound);
template <class T> T __lit(const char *);

template <>
bool _arg<bool>(std::vector<ivy_value> &args, unsigned idx, long long bound) {
    if (!(args[idx].atom == "true" || args[idx].atom == "false") || args[idx].fields.size())
        throw out_of_bounds(idx,args[idx].pos);
    return args[idx].atom == "true";
}

template <>
int _arg<int>(std::vector<ivy_value> &args, unsigned idx, long long bound) {
    std::istringstream s(args[idx].atom.c_str());
    s.unsetf(std::ios::dec);
    s.unsetf(std::ios::hex);
    s.unsetf(std::ios::oct);
    long long res;
    s  >> res;
    // int res = atoi(args[idx].atom.c_str());
    if (bound && (res < 0 || res >= bound) || args[idx].fields.size())
        throw out_of_bounds(idx,args[idx].pos);
    return res;
}

template <>
long long _arg<long long>(std::vector<ivy_value> &args, unsigned idx, long long bound) {
    std::istringstream s(args[idx].atom.c_str());
    s.unsetf(std::ios::dec);
    s.unsetf(std::ios::hex);
    s.unsetf(std::ios::oct);
    long long res;
    s  >> res;
//    long long res = atoll(args[idx].atom.c_str());
    if (bound && (res < 0 || res >= bound) || args[idx].fields.size())
        throw out_of_bounds(idx,args[idx].pos);
    return res;
}

template <>
unsigned long long _arg<unsigned long long>(std::vector<ivy_value> &args, unsigned idx, long long bound) {
    std::istringstream s(args[idx].atom.c_str());
    s.unsetf(std::ios::dec);
    s.unsetf(std::ios::hex);
    s.unsetf(std::ios::oct);
    unsigned long long res;
    s  >> res;
//    unsigned long long res = atoll(args[idx].atom.c_str());
    if (bound && (res < 0 || res >= bound) || args[idx].fields.size())
        throw out_of_bounds(idx,args[idx].pos);
    return res;
}

template <>
unsigned _arg<unsigned>(std::vector<ivy_value> &args, unsigned idx, long long bound) {
    std::istringstream s(args[idx].atom.c_str());
    s.unsetf(std::ios::dec);
    s.unsetf(std::ios::hex);
    s.unsetf(std::ios::oct);
    unsigned res;
    s  >> res;
//    unsigned res = atoll(args[idx].atom.c_str());
    if (bound && (res < 0 || res >= bound) || args[idx].fields.size())
        throw out_of_bounds(idx,args[idx].pos);
    return res;
}


std::ostream &operator <<(std::ostream &s, const __strlit &t){
    s << "\"" << t.c_str() << "\"";
    return s;
}

template <>
__strlit _arg<__strlit>(std::vector<ivy_value> &args, unsigned idx, long long bound) {
    if (args[idx].fields.size())
        throw out_of_bounds(idx,args[idx].pos);
    return args[idx].atom;
}

template <class T> void __ser(ivy_ser &res, const T &inp);

template <>
void __ser<int>(ivy_ser &res, const int &inp) {
    res.set((long long)inp);
}

template <>
void __ser<long long>(ivy_ser &res, const long long &inp) {
    res.set(inp);
}

template <>
void __ser<unsigned long long>(ivy_ser &res, const unsigned long long &inp) {
    res.set((long long)inp);
}

template <>
void __ser<unsigned>(ivy_ser &res, const unsigned &inp) {
    res.set((long long)inp);
}

template <>
void __ser<bool>(ivy_ser &res, const bool &inp) {
    res.set(inp);
}

template <>
void __ser<__strlit>(ivy_ser &res, const __strlit &inp) {
    res.set(inp);
}

template <class T> void __deser(ivy_deser &inp, T &res);

template <>
void __deser<int>(ivy_deser &inp, int &res) {
    long long temp;
    inp.get(temp);
    res = temp;
}

template <>
void __deser<long long>(ivy_deser &inp, long long &res) {
    inp.get(res);
}

template <>
void __deser<unsigned long long>(ivy_deser &inp, unsigned long long &res) {
    long long temp;
    inp.get(temp);
    res = temp;
}

template <>
void __deser<unsigned>(ivy_deser &inp, unsigned &res) {
    long long temp;
    inp.get(temp);
    res = temp;
}

template <>
void __deser<__strlit>(ivy_deser &inp, __strlit &res) {
    inp.get(res);
}

template <>
void __deser<bool>(ivy_deser &inp, bool &res) {
    long long thing;
    inp.get(thing);
    res = thing;
}

class gen;


template <class T> void __from_solver( gen &g, const  z3::expr &v, T &res);

template <>
void __from_solver<int>( gen &g, const  z3::expr &v, int &res) {
    res = g.eval(v);
}

template <>
void __from_solver<long long>( gen &g, const  z3::expr &v, long long &res) {
    res = g.eval(v);
}

template <>
void __from_solver<unsigned long long>( gen &g, const  z3::expr &v, unsigned long long &res) {
    res = g.eval(v);
}

template <>
void __from_solver<unsigned>( gen &g, const  z3::expr &v, unsigned &res) {
    res = g.eval(v);
}

template <>
void __from_solver<bool>( gen &g, const  z3::expr &v, bool &res) {
    res = g.eval(v);
}

template <>
void __from_solver<__strlit>( gen &g, const  z3::expr &v, __strlit &res) {
    res = g.eval_string(v);
}

template <class T>
class to_solver_class {
};

template <class T> z3::expr __to_solver( gen &g, const  z3::expr &v, T &val) {
    return to_solver_class<T>()(g,v,val);
}


template <>
z3::expr __to_solver<int>( gen &g, const  z3::expr &v, int &val) {
    return v == g.int_to_z3(v.get_sort(),val);
}

template <>
z3::expr __to_solver<long long>( gen &g, const  z3::expr &v, long long &val) {
    return v == g.int_to_z3(v.get_sort(),val);
}

template <>
z3::expr __to_solver<unsigned long long>( gen &g, const  z3::expr &v, unsigned long long &val) {
    return v == g.int_to_z3(v.get_sort(),val);
}

template <>
z3::expr __to_solver<unsigned>( gen &g, const  z3::expr &v, unsigned &val) {
    return v == g.int_to_z3(v.get_sort(),val);
}

template <>
z3::expr __to_solver<bool>( gen &g, const  z3::expr &v, bool &val) {
    return v == g.int_to_z3(v.get_sort(),val);
}

template <>
z3::expr __to_solver<__strlit>( gen &g, const  z3::expr &v, __strlit &val) {
//    std::cout << v << ":" << v.get_sort() << std::endl;
    return v == g.int_to_z3(v.get_sort(),val);
}

template <class T>
class __random_string_class {
public:
    std::string operator()() {
        std::string res;
        res.push_back('a' + (rand() % 26)); // no empty strings for now
        while (rand() %2)
            res.push_back('a' + (rand() % 26));
        return res;
    }
};

template <class T> std::string __random_string(){
    return __random_string_class<T>()();
}

template <class T> void __randomize( gen &g, const  z3::expr &v);

template <>
void __randomize<int>( gen &g, const  z3::expr &v) {
    g.randomize(v);
}

template <>
void __randomize<long long>( gen &g, const  z3::expr &v) {
    g.randomize(v);
}

template <>
void __randomize<unsigned long long>( gen &g, const  z3::expr &v) {
    g.randomize(v);
}

template <>
void __randomize<unsigned>( gen &g, const  z3::expr &v) {
    g.randomize(v);
}

template <>
void __randomize<bool>( gen &g, const  z3::expr &v) {
    g.randomize(v);
}

template <>
        void __randomize<__strlit>( gen &g, const  z3::expr &apply_expr) {
    z3::sort range = apply_expr.get_sort();
    __strlit value = (rand() % 2) ? "a" : "b";
    z3::expr val_expr = g.int_to_z3(range,value);
    z3::expr pred = apply_expr == val_expr;
    g.add_alit(pred);
}

template<typename D, typename R>
class z3_thunk : public thunk<D,R> {
    public:
       virtual z3::expr to_z3(gen &g, const  z3::expr &v) = 0;
};

int test_v2::___ivy_choose(int rng,const char *name,int id) {
        std::ostringstream ss;
        ss << name << ':' << id;;
        for (unsigned i = 0; i < ___ivy_stack.size(); i++)
            ss << ':' << ___ivy_stack[i];
        return ___ivy_gen->choose(rng,ss.str().c_str());
    }
void test_v2::ext__protocol__idling(){
    {
        ivy_assume((protocol__idle == (1 & 1)), "test_v2.ivy: line 1078");
        ivy_assert((protocol__idle == (0 & 1)), "test_v2.ivy: line 1083");
    }
}
bool test_v2::selector__execute_r10(){
    bool y;
    y = (bool)___ivy_choose(0,"fml:y",0);
    {
        selector__r10_exec = ((selector__r10_exec + (1 & 255)) & 255);
        if(((selector__r10_rate < selector__r10_exec) || (selector__r10_exec == selector__r10_rate))){
            {
                y = true;
                selector__r10_exec = (0 & 255);
                selector__r10_count = ((selector__r10_count + (1 & 255)) & 255);
            }
        }
        else {
            y = false;
        }
        if(((selector__r10_count_rate < selector__r10_count) || (selector__r10_count == selector__r10_count_rate))){
            {
                selector__r10_stage = ((selector__r10_stage + (1 & 7)) & 7);
                selector__r10_count = (0 & 255);
                selector__r10_count_rate = (4 & 255);
                selector__r10_rate = (39 & 255);
            }
        }
        else {
            if((selector__r10_stage == (1 & 7))){
                {
                    selector__r10_count_rate = (3 & 255);
                    selector__r10_rate = (39 & 255);
                }
            }
            else {
                if((selector__r10_stage == (2 & 7))){
                    {
                        selector__r10_count_rate = (5 & 255);
                        selector__r10_rate = (39 & 255);
                    }
                }
                else {
                    if((selector__r10_stage == (3 & 7))){
                        {
                            selector__r10_count_rate = (4 & 255);
                            selector__r10_rate = (39 & 255);
                        }
                    }
                    else {
                        if((selector__r10_stage == (4 & 7))){
                            {
                                selector__r10_count_rate = (4 & 255);
                                selector__r10_rate = (39 & 255);
                            }
                        }
                        else {
                            if((selector__r10_stage == (5 & 7))){
                                {
                                    selector__r10_count_rate = (5 & 255);
                                    selector__r10_rate = (39 & 255);
                                }
                            }
                            else {
                                if((selector__r10_stage == (6 & 7))){
                                    {
                                        selector__r10_count_rate = (3 & 255);
                                        selector__r10_rate = (39 & 255);
                                    }
                                }
                                else {
                                    if((selector__r10_stage == (7 & 7))){
                                        {
                                            selector__r10_count_rate = (4 & 255);
                                            selector__r10_rate = (39 & 255);
                                        }
                                    }
                                    else {
                                        selector__r10_stage = (0 & 7);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return y;
}
bool test_v2::selector__execute_r11(){
    bool y;
    y = (bool)___ivy_choose(0,"fml:y",0);
    {
        selector__r11_exec = ((selector__r11_exec + (1 & 255)) & 255);
        if(((selector__r11_rate < selector__r11_exec) || (selector__r11_exec == selector__r11_rate))){
            {
                y = true;
                selector__r11_exec = (0 & 255);
                selector__r11_count = ((selector__r11_count + (1 & 255)) & 255);
            }
        }
        else {
            y = false;
        }
        if(((selector__r11_count_rate < selector__r11_count) || (selector__r11_count == selector__r11_count_rate))){
            {
                selector__r11_stage = ((selector__r11_stage + (1 & 7)) & 7);
                selector__r11_count = (0 & 255);
                selector__r11_count_rate = (4 & 255);
                selector__r11_rate = (39 & 255);
            }
        }
        else {
            if((selector__r11_stage == (1 & 7))){
                {
                    selector__r11_count_rate = (3 & 255);
                    selector__r11_rate = (39 & 255);
                }
            }
            else {
                if((selector__r11_stage == (2 & 7))){
                    {
                        selector__r11_count_rate = (5 & 255);
                        selector__r11_rate = (39 & 255);
                    }
                }
                else {
                    if((selector__r11_stage == (3 & 7))){
                        {
                            selector__r11_count_rate = (4 & 255);
                            selector__r11_rate = (39 & 255);
                        }
                    }
                    else {
                        if((selector__r11_stage == (4 & 7))){
                            {
                                selector__r11_count_rate = (4 & 255);
                                selector__r11_rate = (39 & 255);
                            }
                        }
                        else {
                            if((selector__r11_stage == (5 & 7))){
                                {
                                    selector__r11_count_rate = (5 & 255);
                                    selector__r11_rate = (39 & 255);
                                }
                            }
                            else {
                                if((selector__r11_stage == (6 & 7))){
                                    {
                                        selector__r11_count_rate = (3 & 255);
                                        selector__r11_rate = (39 & 255);
                                    }
                                }
                                else {
                                    if((selector__r11_stage == (7 & 7))){
                                        {
                                            selector__r11_count_rate = (4 & 255);
                                            selector__r11_rate = (39 & 255);
                                        }
                                    }
                                    else {
                                        selector__r11_stage = (0 & 7);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return y;
}
bool test_v2::selector__execute_r12(){
    bool y;
    y = (bool)___ivy_choose(0,"fml:y",0);
    {
        selector__r12_exec = ((selector__r12_exec + (1 & 255)) & 255);
        if(((selector__r12_rate < selector__r12_exec) || (selector__r12_exec == selector__r12_rate))){
            {
                y = true;
                selector__r12_exec = (0 & 255);
                selector__r12_count = ((selector__r12_count + (1 & 255)) & 255);
            }
        }
        else {
            y = false;
        }
        if(((selector__r12_count_rate < selector__r12_count) || (selector__r12_count == selector__r12_count_rate))){
            {
                selector__r12_stage = ((selector__r12_stage + (1 & 7)) & 7);
                selector__r12_count = (0 & 255);
                selector__r12_count_rate = (4 & 255);
                selector__r12_rate = (39 & 255);
            }
        }
        else {
            if((selector__r12_stage == (1 & 7))){
                {
                    selector__r12_count_rate = (3 & 255);
                    selector__r12_rate = (39 & 255);
                }
            }
            else {
                if((selector__r12_stage == (2 & 7))){
                    {
                        selector__r12_count_rate = (5 & 255);
                        selector__r12_rate = (39 & 255);
                    }
                }
                else {
                    if((selector__r12_stage == (3 & 7))){
                        {
                            selector__r12_count_rate = (4 & 255);
                            selector__r12_rate = (39 & 255);
                        }
                    }
                    else {
                        if((selector__r12_stage == (4 & 7))){
                            {
                                selector__r12_count_rate = (4 & 255);
                                selector__r12_rate = (39 & 255);
                            }
                        }
                        else {
                            if((selector__r12_stage == (5 & 7))){
                                {
                                    selector__r12_count_rate = (5 & 255);
                                    selector__r12_rate = (39 & 255);
                                }
                            }
                            else {
                                if((selector__r12_stage == (6 & 7))){
                                    {
                                        selector__r12_count_rate = (3 & 255);
                                        selector__r12_rate = (39 & 255);
                                    }
                                }
                                else {
                                    if((selector__r12_stage == (7 & 7))){
                                        {
                                            selector__r12_count_rate = (4 & 255);
                                            selector__r12_rate = (39 & 255);
                                        }
                                    }
                                    else {
                                        selector__r12_stage = (0 & 7);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return y;
}
void test_v2::ext__protocol__fail_test(){
    {
        ivy_assume((protocol__idle == (0 & 1)), "test_v2.ivy: line 1089");
        {
            bool loc__0;
    loc__0 = (bool)___ivy_choose(0,"loc:0",216);
            bool loc__1;
    loc__1 = (bool)___ivy_choose(0,"loc:1",216);
            bool loc__2;
    loc__2 = (bool)___ivy_choose(0,"loc:2",216);
            bool loc__3;
    loc__3 = (bool)___ivy_choose(0,"loc:3",216);
            bool loc__4;
    loc__4 = (bool)___ivy_choose(0,"loc:4",216);
            bool loc__5;
    loc__5 = (bool)___ivy_choose(0,"loc:5",216);
            bool loc__6;
    loc__6 = (bool)___ivy_choose(0,"loc:6",216);
            bool loc__7;
    loc__7 = (bool)___ivy_choose(0,"loc:7",216);
            bool loc__8;
    loc__8 = (bool)___ivy_choose(0,"loc:8",216);
            bool loc__9;
    loc__9 = (bool)___ivy_choose(0,"loc:9",216);
            bool loc__10;
    loc__10 = (bool)___ivy_choose(0,"loc:10",216);
            bool loc__11;
    loc__11 = (bool)___ivy_choose(0,"loc:11",216);
            {
                ___ivy_stack.push_back(646);
                loc__0 = enabled_checker__is_enabled_r1(protocol__r_codY);
                ___ivy_stack.pop_back();
                ___ivy_stack.push_back(647);
                loc__1 = enabled_checker__is_enabled_r2(protocol__r_CodY);
                ___ivy_stack.pop_back();
                ___ivy_stack.push_back(648);
                loc__2 = enabled_checker__is_enabled_r3(protocol__r_flache);
                ___ivy_stack.pop_back();
                ___ivy_stack.push_back(649);
                loc__3 = enabled_checker__is_enabled_r4(protocol__r_SigD);
                ___ivy_stack.pop_back();
                ___ivy_stack.push_back(650);
                loc__4 = enabled_checker__is_enabled_r5(protocol__r_SigD_hag);
                ___ivy_stack.pop_back();
                ___ivy_stack.push_back(651);
                loc__5 = enabled_checker__is_enabled_r6(protocol__r_Hag);
                ___ivy_stack.pop_back();
                ___ivy_stack.push_back(652);
                loc__6 = enabled_checker__is_enabled_r7(protocol__r_SigD, protocol__r_hag);
                ___ivy_stack.pop_back();
                ___ivy_stack.push_back(653);
                loc__7 = enabled_checker__is_enabled_r8(protocol__r_SigD_hag);
                ___ivy_stack.pop_back();
                ___ivy_stack.push_back(654);
                loc__8 = enabled_checker__is_enabled_r9(protocol__r_CodY, protocol__r_flache);
                ___ivy_stack.pop_back();
                ___ivy_stack.push_back(655);
                loc__9 = enabled_checker__is_enabled_r10(protocol__r_CodY_flache);
                ___ivy_stack.pop_back();
                ___ivy_stack.push_back(656);
                loc__10 = enabled_checker__is_enabled_r11(protocol__r_CodY, protocol__r_hag);
                ___ivy_stack.pop_back();
                ___ivy_stack.push_back(657);
                loc__11 = enabled_checker__is_enabled_r12(protocol__r_CodY_hag);
                ___ivy_stack.pop_back();
                ivy_assume(((loc__0 == false) && (loc__1 == false) && (loc__2 == false) && (loc__3 == false) && (loc__4 == false) && (loc__5 == false) && (loc__6 == false) && (loc__7 == false) && (loc__8 == false) && (loc__9 == false) && (loc__10 == false) && (loc__11 == false)), "test_v2.ivy: line 1090");
            }
        }
        ivy_assert(false, "test_v2.ivy: line 1094");
    }
}
void test_v2::ext__protocol__update_r9(){
    {
        ivy_assume((protocol__idle == (0 & 1)), "test_v2.ivy: line 1060");
        {
            bool loc__0;
    loc__0 = (bool)___ivy_choose(0,"loc:0",217);
            {
                ___ivy_stack.push_back(658);
                loc__0 = enabled_checker__is_enabled_r9(protocol__r_CodY, protocol__r_flache);
                ___ivy_stack.pop_back();
                ivy_assume(loc__0, "test_v2.ivy: line 1061");
            }
        }
        {
            bool loc__0;
    loc__0 = (bool)___ivy_choose(0,"loc:0",218);
            {
                ___ivy_stack.push_back(659);
                loc__0 = selector__execute_r9();
                ___ivy_stack.pop_back();
                if(loc__0){
                    {
                        ___ivy_stack.push_back(660);
                        inspector__check_guard_r9(protocol__r_CodY, protocol__r_flache);
                        ___ivy_stack.pop_back();
                        ___ivy_stack.push_back(661);
                        protocol__r_CodY = updater__decr(protocol__r_CodY);
                        ___ivy_stack.pop_back();
                        ___ivy_stack.push_back(662);
                        protocol__r_flache = updater__decr(protocol__r_flache);
                        ___ivy_stack.pop_back();
                        ___ivy_stack.push_back(663);
                        protocol__r_CodY_flache = updater__incr(protocol__r_CodY_flache);
                        ___ivy_stack.pop_back();
                    }
                }
            }
        }
    }
}
void test_v2::ext__protocol__update_r8(){
    {
        ivy_assume((protocol__idle == (0 & 1)), "test_v2.ivy: line 1056");
        {
            bool loc__0;
    loc__0 = (bool)___ivy_choose(0,"loc:0",219);
            {
                ___ivy_stack.push_back(664);
                loc__0 = enabled_checker__is_enabled_r8(protocol__r_SigD_hag);
                ___ivy_stack.pop_back();
                ivy_assume(loc__0, "test_v2.ivy: line 1057");
            }
        }
        {
            bool loc__0;
    loc__0 = (bool)___ivy_choose(0,"loc:0",220);
            {
                ___ivy_stack.push_back(665);
                loc__0 = selector__execute_r8();
                ___ivy_stack.pop_back();
                if(loc__0){
                    {
                        ___ivy_stack.push_back(666);
                        inspector__check_guard_r8(protocol__r_SigD_hag);
                        ___ivy_stack.pop_back();
                        ___ivy_stack.push_back(667);
                        protocol__r_SigD_hag = updater__decr(protocol__r_SigD_hag);
                        ___ivy_stack.pop_back();
                        ___ivy_stack.push_back(668);
                        protocol__r_SigD = updater__incr(protocol__r_SigD);
                        ___ivy_stack.pop_back();
                        ___ivy_stack.push_back(669);
                        protocol__r_hag = updater__incr(protocol__r_hag);
                        ___ivy_stack.pop_back();
                    }
                }
            }
        }
    }
}
bool test_v2::selector__execute_r8(){
    bool y;
    y = (bool)___ivy_choose(0,"fml:y",0);
    {
        selector__r8_exec = ((selector__r8_exec + (1 & 255)) & 255);
        if(((selector__r8_rate < selector__r8_exec) || (selector__r8_exec == selector__r8_rate))){
            {
                y = true;
                selector__r8_exec = (0 & 255);
                selector__r8_count = ((selector__r8_count + (1 & 255)) & 255);
            }
        }
        else {
            y = false;
        }
        if(((selector__r8_count_rate < selector__r8_count) || (selector__r8_count == selector__r8_count_rate))){
            {
                selector__r8_stage = ((selector__r8_stage + (1 & 7)) & 7);
                selector__r8_count = (0 & 255);
                selector__r8_count_rate = (4 & 255);
                selector__r8_rate = (39 & 255);
            }
        }
        else {
            if((selector__r8_stage == (1 & 7))){
                {
                    selector__r8_count_rate = (3 & 255);
                    selector__r8_rate = (39 & 255);
                }
            }
            else {
                if((selector__r8_stage == (2 & 7))){
                    {
                        selector__r8_count_rate = (5 & 255);
                        selector__r8_rate = (39 & 255);
                    }
                }
                else {
                    if((selector__r8_stage == (3 & 7))){
                        {
                            selector__r8_count_rate = (4 & 255);
                            selector__r8_rate = (39 & 255);
                        }
                    }
                    else {
                        if((selector__r8_stage == (4 & 7))){
                            {
                                selector__r8_count_rate = (4 & 255);
                                selector__r8_rate = (39 & 255);
                            }
                        }
                        else {
                            if((selector__r8_stage == (5 & 7))){
                                {
                                    selector__r8_count_rate = (5 & 255);
                                    selector__r8_rate = (39 & 255);
                                }
                            }
                            else {
                                if((selector__r8_stage == (6 & 7))){
                                    {
                                        selector__r8_count_rate = (3 & 255);
                                        selector__r8_rate = (39 & 255);
                                    }
                                }
                                else {
                                    if((selector__r8_stage == (7 & 7))){
                                        {
                                            selector__r8_count_rate = (4 & 255);
                                            selector__r8_rate = (39 & 255);
                                        }
                                    }
                                    else {
                                        selector__r8_stage = (0 & 7);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return y;
}
bool test_v2::selector__execute_r9(){
    bool y;
    y = (bool)___ivy_choose(0,"fml:y",0);
    {
        selector__r9_exec = ((selector__r9_exec + (1 & 255)) & 255);
        if(((selector__r9_rate < selector__r9_exec) || (selector__r9_exec == selector__r9_rate))){
            {
                y = true;
                selector__r9_exec = (0 & 255);
                selector__r9_count = ((selector__r9_count + (1 & 255)) & 255);
            }
        }
        else {
            y = false;
        }
        if(((selector__r9_count_rate < selector__r9_count) || (selector__r9_count == selector__r9_count_rate))){
            {
                selector__r9_stage = ((selector__r9_stage + (1 & 7)) & 7);
                selector__r9_count = (0 & 255);
                selector__r9_count_rate = (4 & 255);
                selector__r9_rate = (39 & 255);
            }
        }
        else {
            if((selector__r9_stage == (1 & 7))){
                {
                    selector__r9_count_rate = (3 & 255);
                    selector__r9_rate = (39 & 255);
                }
            }
            else {
                if((selector__r9_stage == (2 & 7))){
                    {
                        selector__r9_count_rate = (5 & 255);
                        selector__r9_rate = (39 & 255);
                    }
                }
                else {
                    if((selector__r9_stage == (3 & 7))){
                        {
                            selector__r9_count_rate = (4 & 255);
                            selector__r9_rate = (39 & 255);
                        }
                    }
                    else {
                        if((selector__r9_stage == (4 & 7))){
                            {
                                selector__r9_count_rate = (4 & 255);
                                selector__r9_rate = (39 & 255);
                            }
                        }
                        else {
                            if((selector__r9_stage == (5 & 7))){
                                {
                                    selector__r9_count_rate = (5 & 255);
                                    selector__r9_rate = (39 & 255);
                                }
                            }
                            else {
                                if((selector__r9_stage == (6 & 7))){
                                    {
                                        selector__r9_count_rate = (3 & 255);
                                        selector__r9_rate = (39 & 255);
                                    }
                                }
                                else {
                                    if((selector__r9_stage == (7 & 7))){
                                        {
                                            selector__r9_count_rate = (4 & 255);
                                            selector__r9_rate = (39 & 255);
                                        }
                                    }
                                    else {
                                        selector__r9_stage = (0 & 7);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return y;
}
void test_v2::ext__protocol__update_r1(){
    {
        ivy_assume((protocol__idle == (0 & 1)), "test_v2.ivy: line 1028");
        {
            bool loc__0;
    loc__0 = (bool)___ivy_choose(0,"loc:0",237);
            {
                ___ivy_stack.push_back(725);
                loc__0 = enabled_checker__is_enabled_r1(protocol__r_codY);
                ___ivy_stack.pop_back();
                ivy_assume(loc__0, "test_v2.ivy: line 1029");
            }
        }
        {
            bool loc__0;
    loc__0 = (bool)___ivy_choose(0,"loc:0",238);
            {
                ___ivy_stack.push_back(726);
                loc__0 = selector__execute_r1();
                ___ivy_stack.pop_back();
                if(loc__0){
                    {
                        ___ivy_stack.push_back(727);
                        inspector__check_guard_r1(protocol__r_codY);
                        ___ivy_stack.pop_back();
                        ___ivy_stack.push_back(728);
                        protocol__r_codY = updater__decr(protocol__r_codY);
                        ___ivy_stack.pop_back();
                        ___ivy_stack.push_back(729);
                        protocol__r_codY = updater__incr(protocol__r_codY);
                        ___ivy_stack.pop_back();
                        ___ivy_stack.push_back(730);
                        protocol__r_CodY = updater__incr(protocol__r_CodY);
                        ___ivy_stack.pop_back();
                        if((((20 & 1023) < protocol__r_CodY) || (protocol__r_CodY == (20 & 1023)))){
                            ___ivy_stack.push_back(731);
                            goal__achieved(protocol__r_CodY);
                            ___ivy_stack.pop_back();
                        }
                    }
                }
            }
        }
    }
}
bool test_v2::selector__execute_r3(){
    bool y;
    y = (bool)___ivy_choose(0,"fml:y",0);
    {
        selector__r3_exec = ((selector__r3_exec + (1 & 255)) & 255);
        if(((selector__r3_rate < selector__r3_exec) || (selector__r3_exec == selector__r3_rate))){
            {
                y = true;
                selector__r3_exec = (0 & 255);
                selector__r3_count = ((selector__r3_count + (1 & 255)) & 255);
            }
        }
        else {
            y = false;
        }
        if(((selector__r3_count_rate < selector__r3_count) || (selector__r3_count == selector__r3_count_rate))){
            {
                selector__r3_stage = ((selector__r3_stage + (1 & 7)) & 7);
                selector__r3_count = (0 & 255);
                selector__r3_count_rate = (4 & 255);
                selector__r3_rate = (39 & 255);
            }
        }
        else {
            if((selector__r3_stage == (1 & 7))){
                {
                    selector__r3_count_rate = (3 & 255);
                    selector__r3_rate = (39 & 255);
                }
            }
            else {
                if((selector__r3_stage == (2 & 7))){
                    {
                        selector__r3_count_rate = (5 & 255);
                        selector__r3_rate = (39 & 255);
                    }
                }
                else {
                    if((selector__r3_stage == (3 & 7))){
                        {
                            selector__r3_count_rate = (4 & 255);
                            selector__r3_rate = (39 & 255);
                        }
                    }
                    else {
                        if((selector__r3_stage == (4 & 7))){
                            {
                                selector__r3_count_rate = (4 & 255);
                                selector__r3_rate = (39 & 255);
                            }
                        }
                        else {
                            if((selector__r3_stage == (5 & 7))){
                                {
                                    selector__r3_count_rate = (5 & 255);
                                    selector__r3_rate = (39 & 255);
                                }
                            }
                            else {
                                if((selector__r3_stage == (6 & 7))){
                                    {
                                        selector__r3_count_rate = (3 & 255);
                                        selector__r3_rate = (39 & 255);
                                    }
                                }
                                else {
                                    if((selector__r3_stage == (7 & 7))){
                                        {
                                            selector__r3_count_rate = (4 & 255);
                                            selector__r3_rate = (39 & 255);
                                        }
                                    }
                                    else {
                                        selector__r3_stage = (0 & 7);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return y;
}
void test_v2::ext__protocol__update_r3(){
    {
        ivy_assume((protocol__idle == (0 & 1)), "test_v2.ivy: line 1036");
        {
            bool loc__0;
    loc__0 = (bool)___ivy_choose(0,"loc:0",221);
            {
                ___ivy_stack.push_back(670);
                loc__0 = enabled_checker__is_enabled_r3(protocol__r_flache);
                ___ivy_stack.pop_back();
                ivy_assume(loc__0, "test_v2.ivy: line 1037");
            }
        }
        {
            bool loc__0;
    loc__0 = (bool)___ivy_choose(0,"loc:0",222);
            {
                ___ivy_stack.push_back(671);
                loc__0 = selector__execute_r3();
                ___ivy_stack.pop_back();
                if(loc__0){
                    {
                        ___ivy_stack.push_back(672);
                        inspector__check_guard_r3(protocol__r_flache);
                        ___ivy_stack.pop_back();
                        ___ivy_stack.push_back(673);
                        protocol__r_flache = updater__decr(protocol__r_flache);
                        ___ivy_stack.pop_back();
                        ___ivy_stack.push_back(674);
                        protocol__r_flache = updater__incr(protocol__r_flache);
                        ___ivy_stack.pop_back();
                        ___ivy_stack.push_back(675);
                        protocol__r_SigD = updater__incr(protocol__r_SigD);
                        ___ivy_stack.pop_back();
                    }
                }
            }
        }
    }
}
void test_v2::ext__protocol__update_r2(){
    {
        ivy_assume((protocol__idle == (0 & 1)), "test_v2.ivy: line 1032");
        {
            bool loc__0;
    loc__0 = (bool)___ivy_choose(0,"loc:0",223);
            {
                ___ivy_stack.push_back(676);
                loc__0 = enabled_checker__is_enabled_r2(protocol__r_CodY);
                ___ivy_stack.pop_back();
                ivy_assume(loc__0, "test_v2.ivy: line 1033");
            }
        }
        {
            bool loc__0;
    loc__0 = (bool)___ivy_choose(0,"loc:0",224);
            {
                ___ivy_stack.push_back(677);
                loc__0 = selector__execute_r2();
                ___ivy_stack.pop_back();
                if(loc__0){
                    {
                        ___ivy_stack.push_back(678);
                        inspector__check_guard_r2(protocol__r_CodY);
                        ___ivy_stack.pop_back();
                        ___ivy_stack.push_back(679);
                        protocol__r_CodY = updater__decr(protocol__r_CodY);
                        ___ivy_stack.pop_back();
                    }
                }
            }
        }
    }
}
void test_v2::ext__protocol__update_r5(){
    {
        ivy_assume((protocol__idle == (0 & 1)), "test_v2.ivy: line 1044");
        {
            bool loc__0;
    loc__0 = (bool)___ivy_choose(0,"loc:0",225);
            {
                ___ivy_stack.push_back(680);
                loc__0 = enabled_checker__is_enabled_r5(protocol__r_SigD_hag);
                ___ivy_stack.pop_back();
                ivy_assume(loc__0, "test_v2.ivy: line 1045");
            }
        }
        {
            bool loc__0;
    loc__0 = (bool)___ivy_choose(0,"loc:0",226);
            {
                ___ivy_stack.push_back(681);
                loc__0 = selector__execute_r5();
                ___ivy_stack.pop_back();
                if(loc__0){
                    {
                        ___ivy_stack.push_back(682);
                        inspector__check_guard_r5(protocol__r_SigD_hag);
                        ___ivy_stack.pop_back();
                        ___ivy_stack.push_back(683);
                        protocol__r_SigD_hag = updater__decr(protocol__r_SigD_hag);
                        ___ivy_stack.pop_back();
                        ___ivy_stack.push_back(684);
                        protocol__r_SigD = updater__incr(protocol__r_SigD);
                        ___ivy_stack.pop_back();
                        ___ivy_stack.push_back(685);
                        protocol__r_hag = updater__incr(protocol__r_hag);
                        ___ivy_stack.pop_back();
                        ___ivy_stack.push_back(686);
                        protocol__r_Hag = updater__incr(protocol__r_Hag);
                        ___ivy_stack.pop_back();
                    }
                }
            }
        }
    }
}
void test_v2::ext__protocol__update_r4(){
    {
        ivy_assume((protocol__idle == (0 & 1)), "test_v2.ivy: line 1040");
        {
            bool loc__0;
    loc__0 = (bool)___ivy_choose(0,"loc:0",239);
            {
                ___ivy_stack.push_back(734);
                loc__0 = enabled_checker__is_enabled_r4(protocol__r_SigD);
                ___ivy_stack.pop_back();
                ivy_assume(loc__0, "test_v2.ivy: line 1041");
            }
        }
        {
            bool loc__0;
    loc__0 = (bool)___ivy_choose(0,"loc:0",240);
            {
                ___ivy_stack.push_back(735);
                loc__0 = selector__execute_r4();
                ___ivy_stack.pop_back();
                if(loc__0){
                    {
                        ___ivy_stack.push_back(736);
                        inspector__check_guard_r4(protocol__r_SigD);
                        ___ivy_stack.pop_back();
                        ___ivy_stack.push_back(737);
                        protocol__r_SigD = updater__decr(protocol__r_SigD);
                        ___ivy_stack.pop_back();
                    }
                }
            }
        }
    }
}
bool test_v2::selector__execute_r4(){
    bool y;
    y = (bool)___ivy_choose(0,"fml:y",0);
    {
        selector__r4_exec = ((selector__r4_exec + (1 & 255)) & 255);
        if(((selector__r4_rate < selector__r4_exec) || (selector__r4_exec == selector__r4_rate))){
            {
                y = true;
                selector__r4_exec = (0 & 255);
                selector__r4_count = ((selector__r4_count + (1 & 255)) & 255);
            }
        }
        else {
            y = false;
        }
        if(((selector__r4_count_rate < selector__r4_count) || (selector__r4_count == selector__r4_count_rate))){
            {
                selector__r4_stage = ((selector__r4_stage + (1 & 7)) & 7);
                selector__r4_count = (0 & 255);
                selector__r4_count_rate = (4 & 255);
                selector__r4_rate = (39 & 255);
            }
        }
        else {
            if((selector__r4_stage == (1 & 7))){
                {
                    selector__r4_count_rate = (3 & 255);
                    selector__r4_rate = (39 & 255);
                }
            }
            else {
                if((selector__r4_stage == (2 & 7))){
                    {
                        selector__r4_count_rate = (5 & 255);
                        selector__r4_rate = (39 & 255);
                    }
                }
                else {
                    if((selector__r4_stage == (3 & 7))){
                        {
                            selector__r4_count_rate = (4 & 255);
                            selector__r4_rate = (39 & 255);
                        }
                    }
                    else {
                        if((selector__r4_stage == (4 & 7))){
                            {
                                selector__r4_count_rate = (4 & 255);
                                selector__r4_rate = (39 & 255);
                            }
                        }
                        else {
                            if((selector__r4_stage == (5 & 7))){
                                {
                                    selector__r4_count_rate = (5 & 255);
                                    selector__r4_rate = (39 & 255);
                                }
                            }
                            else {
                                if((selector__r4_stage == (6 & 7))){
                                    {
                                        selector__r4_count_rate = (3 & 255);
                                        selector__r4_rate = (39 & 255);
                                    }
                                }
                                else {
                                    if((selector__r4_stage == (7 & 7))){
                                        {
                                            selector__r4_count_rate = (4 & 255);
                                            selector__r4_rate = (39 & 255);
                                        }
                                    }
                                    else {
                                        selector__r4_stage = (0 & 7);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return y;
}
bool test_v2::selector__execute_r5(){
    bool y;
    y = (bool)___ivy_choose(0,"fml:y",0);
    {
        selector__r5_exec = ((selector__r5_exec + (1 & 255)) & 255);
        if(((selector__r5_rate < selector__r5_exec) || (selector__r5_exec == selector__r5_rate))){
            {
                y = true;
                selector__r5_exec = (0 & 255);
                selector__r5_count = ((selector__r5_count + (1 & 255)) & 255);
            }
        }
        else {
            y = false;
        }
        if(((selector__r5_count_rate < selector__r5_count) || (selector__r5_count == selector__r5_count_rate))){
            {
                selector__r5_stage = ((selector__r5_stage + (1 & 7)) & 7);
                selector__r5_count = (0 & 255);
                selector__r5_count_rate = (4 & 255);
                selector__r5_rate = (39 & 255);
            }
        }
        else {
            if((selector__r5_stage == (1 & 7))){
                {
                    selector__r5_count_rate = (3 & 255);
                    selector__r5_rate = (39 & 255);
                }
            }
            else {
                if((selector__r5_stage == (2 & 7))){
                    {
                        selector__r5_count_rate = (5 & 255);
                        selector__r5_rate = (39 & 255);
                    }
                }
                else {
                    if((selector__r5_stage == (3 & 7))){
                        {
                            selector__r5_count_rate = (4 & 255);
                            selector__r5_rate = (39 & 255);
                        }
                    }
                    else {
                        if((selector__r5_stage == (4 & 7))){
                            {
                                selector__r5_count_rate = (4 & 255);
                                selector__r5_rate = (39 & 255);
                            }
                        }
                        else {
                            if((selector__r5_stage == (5 & 7))){
                                {
                                    selector__r5_count_rate = (5 & 255);
                                    selector__r5_rate = (39 & 255);
                                }
                            }
                            else {
                                if((selector__r5_stage == (6 & 7))){
                                    {
                                        selector__r5_count_rate = (3 & 255);
                                        selector__r5_rate = (39 & 255);
                                    }
                                }
                                else {
                                    if((selector__r5_stage == (7 & 7))){
                                        {
                                            selector__r5_count_rate = (4 & 255);
                                            selector__r5_rate = (39 & 255);
                                        }
                                    }
                                    else {
                                        selector__r5_stage = (0 & 7);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return y;
}
bool test_v2::enabled_checker__is_enabled_r7(unsigned reactant1, unsigned reactant2){
    bool y;
    y = (bool)___ivy_choose(0,"fml:y",0);
    if(((((1 & 1023) < reactant1) || (reactant1 == (1 & 1023))) && (((1 & 1023) < reactant2) || (reactant2 == (1 & 1023))))){
        y = true;
    }
    else {
        y = false;
    }
    return y;
}
bool test_v2::enabled_checker__is_enabled_r6(unsigned reactant1){
    bool y;
    y = (bool)___ivy_choose(0,"fml:y",0);
    if((((1 & 1023) < reactant1) || (reactant1 == (1 & 1023)))){
        y = true;
    }
    else {
        y = false;
    }
    return y;
}
bool test_v2::enabled_checker__is_enabled_r4(unsigned reactant1){
    bool y;
    y = (bool)___ivy_choose(0,"fml:y",0);
    if((((1 & 1023) < reactant1) || (reactant1 == (1 & 1023)))){
        y = true;
    }
    else {
        y = false;
    }
    return y;
}
void test_v2::goal__achieved(unsigned v){
    __ivy_out  << "< goal.achieved" << "(" << v << ")" << std::endl;
    {
        ivy_assert((((20 & 1023) < v) || (v == (20 & 1023))), "test_v2.ivy: line 25");
        protocol__idle = (1 & 1);
        ___ivy_stack.push_back(715);
        imp__goal__achieved(v);
        ___ivy_stack.pop_back();
    }
}
void test_v2::inspector__check_guard_r4(unsigned reactant1){
    __ivy_out  << "< inspector.check_guard_r4" << "(" << reactant1 << ")" << std::endl;
    {
        ivy_assert((((1 & 1023) < reactant1) || (reactant1 == (1 & 1023))), "test_v2.ivy: line 162");
        ___ivy_stack.push_back(716);
        imp__inspector__check_guard_r4(reactant1);
        ___ivy_stack.pop_back();
    }
}
void test_v2::inspector__check_guard_r5(unsigned reactant1){
    __ivy_out  << "< inspector.check_guard_r5" << "(" << reactant1 << ")" << std::endl;
    {
        ivy_assert((((1 & 1023) < reactant1) || (reactant1 == (1 & 1023))), "test_v2.ivy: line 167");
        ___ivy_stack.push_back(717);
        imp__inspector__check_guard_r5(reactant1);
        ___ivy_stack.pop_back();
    }
}
void test_v2::inspector__check_guard_r6(unsigned reactant1){
    __ivy_out  << "< inspector.check_guard_r6" << "(" << reactant1 << ")" << std::endl;
    {
        ivy_assert((((1 & 1023) < reactant1) || (reactant1 == (1 & 1023))), "test_v2.ivy: line 172");
        ___ivy_stack.push_back(718);
        imp__inspector__check_guard_r6(reactant1);
        ___ivy_stack.pop_back();
    }
}
void test_v2::inspector__check_guard_r7(unsigned reactant1, unsigned reactant2){
    __ivy_out  << "< inspector.check_guard_r7" << "(" << reactant1 << "," << reactant2 << ")" << std::endl;
    {
        ivy_assert(((((1 & 1023) < reactant1) || (reactant1 == (1 & 1023))) && (((1 & 1023) < reactant2) || (reactant2 == (1 & 1023)))), "test_v2.ivy: line 177");
        ___ivy_stack.push_back(719);
        imp__inspector__check_guard_r7(reactant1, reactant2);
        ___ivy_stack.pop_back();
    }
}
void test_v2::inspector__check_guard_r1(unsigned reactant1){
    __ivy_out  << "< inspector.check_guard_r1" << "(" << reactant1 << ")" << std::endl;
    {
        ivy_assert((((1 & 1023) < reactant1) || (reactant1 == (1 & 1023))), "test_v2.ivy: line 147");
        ___ivy_stack.push_back(720);
        imp__inspector__check_guard_r1(reactant1);
        ___ivy_stack.pop_back();
    }
}
void test_v2::inspector__check_guard_r2(unsigned reactant1){
    __ivy_out  << "< inspector.check_guard_r2" << "(" << reactant1 << ")" << std::endl;
    {
        ivy_assert((((1 & 1023) < reactant1) || (reactant1 == (1 & 1023))), "test_v2.ivy: line 152");
        ___ivy_stack.push_back(721);
        imp__inspector__check_guard_r2(reactant1);
        ___ivy_stack.pop_back();
    }
}
void test_v2::inspector__check_guard_r3(unsigned reactant1){
    __ivy_out  << "< inspector.check_guard_r3" << "(" << reactant1 << ")" << std::endl;
    {
        ivy_assert((((1 & 1023) < reactant1) || (reactant1 == (1 & 1023))), "test_v2.ivy: line 157");
        ___ivy_stack.push_back(722);
        imp__inspector__check_guard_r3(reactant1);
        ___ivy_stack.pop_back();
    }
}
bool test_v2::enabled_checker__is_enabled_r12(unsigned reactant1){
    bool y;
    y = (bool)___ivy_choose(0,"fml:y",0);
    if((((1 & 1023) < reactant1) || (reactant1 == (1 & 1023)))){
        y = true;
    }
    else {
        y = false;
    }
    return y;
}
bool test_v2::enabled_checker__is_enabled_r11(unsigned reactant1, unsigned reactant2){
    bool y;
    y = (bool)___ivy_choose(0,"fml:y",0);
    if(((((1 & 1023) < reactant1) || (reactant1 == (1 & 1023))) && (((1 & 1023) < reactant2) || (reactant2 == (1 & 1023))))){
        y = true;
    }
    else {
        y = false;
    }
    return y;
}
bool test_v2::enabled_checker__is_enabled_r10(unsigned reactant1){
    bool y;
    y = (bool)___ivy_choose(0,"fml:y",0);
    if((((1 & 1023) < reactant1) || (reactant1 == (1 & 1023)))){
        y = true;
    }
    else {
        y = false;
    }
    return y;
}
void test_v2::inspector__check_guard_r8(unsigned reactant1){
    __ivy_out  << "< inspector.check_guard_r8" << "(" << reactant1 << ")" << std::endl;
    {
        ivy_assert((((1 & 1023) < reactant1) || (reactant1 == (1 & 1023))), "test_v2.ivy: line 182");
        ___ivy_stack.push_back(723);
        imp__inspector__check_guard_r8(reactant1);
        ___ivy_stack.pop_back();
    }
}
void test_v2::inspector__check_guard_r9(unsigned reactant1, unsigned reactant2){
    __ivy_out  << "< inspector.check_guard_r9" << "(" << reactant1 << "," << reactant2 << ")" << std::endl;
    {
        ivy_assert(((((1 & 1023) < reactant1) || (reactant1 == (1 & 1023))) && (((1 & 1023) < reactant2) || (reactant2 == (1 & 1023)))), "test_v2.ivy: line 187");
        ___ivy_stack.push_back(724);
        imp__inspector__check_guard_r9(reactant1, reactant2);
        ___ivy_stack.pop_back();
    }
}
bool test_v2::selector__execute_r2(){
    bool y;
    y = (bool)___ivy_choose(0,"fml:y",0);
    {
        selector__r2_exec = ((selector__r2_exec + (1 & 255)) & 255);
        if(((selector__r2_rate < selector__r2_exec) || (selector__r2_exec == selector__r2_rate))){
            {
                y = true;
                selector__r2_exec = (0 & 255);
                selector__r2_count = ((selector__r2_count + (1 & 255)) & 255);
            }
        }
        else {
            y = false;
        }
        if(((selector__r2_count_rate < selector__r2_count) || (selector__r2_count == selector__r2_count_rate))){
            {
                selector__r2_stage = ((selector__r2_stage + (1 & 7)) & 7);
                selector__r2_count = (0 & 255);
                selector__r2_count_rate = (4 & 255);
                selector__r2_rate = (39 & 255);
            }
        }
        else {
            if((selector__r2_stage == (1 & 7))){
                {
                    selector__r2_count_rate = (3 & 255);
                    selector__r2_rate = (39 & 255);
                }
            }
            else {
                if((selector__r2_stage == (2 & 7))){
                    {
                        selector__r2_count_rate = (5 & 255);
                        selector__r2_rate = (39 & 255);
                    }
                }
                else {
                    if((selector__r2_stage == (3 & 7))){
                        {
                            selector__r2_count_rate = (4 & 255);
                            selector__r2_rate = (39 & 255);
                        }
                    }
                    else {
                        if((selector__r2_stage == (4 & 7))){
                            {
                                selector__r2_count_rate = (4 & 255);
                                selector__r2_rate = (39 & 255);
                            }
                        }
                        else {
                            if((selector__r2_stage == (5 & 7))){
                                {
                                    selector__r2_count_rate = (5 & 255);
                                    selector__r2_rate = (39 & 255);
                                }
                            }
                            else {
                                if((selector__r2_stage == (6 & 7))){
                                    {
                                        selector__r2_count_rate = (3 & 255);
                                        selector__r2_rate = (39 & 255);
                                    }
                                }
                                else {
                                    if((selector__r2_stage == (7 & 7))){
                                        {
                                            selector__r2_count_rate = (4 & 255);
                                            selector__r2_rate = (39 & 255);
                                        }
                                    }
                                    else {
                                        selector__r2_stage = (0 & 7);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return y;
}
bool test_v2::enabled_checker__is_enabled_r9(unsigned reactant1, unsigned reactant2){
    bool y;
    y = (bool)___ivy_choose(0,"fml:y",0);
    if(((((1 & 1023) < reactant1) || (reactant1 == (1 & 1023))) && (((1 & 1023) < reactant2) || (reactant2 == (1 & 1023))))){
        y = true;
    }
    else {
        y = false;
    }
    return y;
}
bool test_v2::enabled_checker__is_enabled_r8(unsigned reactant1){
    bool y;
    y = (bool)___ivy_choose(0,"fml:y",0);
    if((((1 & 1023) < reactant1) || (reactant1 == (1 & 1023)))){
        y = true;
    }
    else {
        y = false;
    }
    return y;
}
bool test_v2::enabled_checker__is_enabled_r3(unsigned reactant1){
    bool y;
    y = (bool)___ivy_choose(0,"fml:y",0);
    if((((1 & 1023) < reactant1) || (reactant1 == (1 & 1023)))){
        y = true;
    }
    else {
        y = false;
    }
    return y;
}
bool test_v2::enabled_checker__is_enabled_r2(unsigned reactant1){
    bool y;
    y = (bool)___ivy_choose(0,"fml:y",0);
    if((((1 & 1023) < reactant1) || (reactant1 == (1 & 1023)))){
        y = true;
    }
    else {
        y = false;
    }
    return y;
}
bool test_v2::enabled_checker__is_enabled_r1(unsigned reactant1){
    bool y;
    y = (bool)___ivy_choose(0,"fml:y",0);
    if((((1 & 1023) < reactant1) || (reactant1 == (1 & 1023)))){
        y = true;
    }
    else {
        y = false;
    }
    return y;
}
void test_v2::ext__protocol__update_r11(){
    {
        ivy_assume((protocol__idle == (0 & 1)), "test_v2.ivy: line 1068");
        {
            bool loc__0;
    loc__0 = (bool)___ivy_choose(0,"loc:0",231);
            {
                ___ivy_stack.push_back(697);
                loc__0 = enabled_checker__is_enabled_r11(protocol__r_CodY, protocol__r_hag);
                ___ivy_stack.pop_back();
                ivy_assume(loc__0, "test_v2.ivy: line 1069");
            }
        }
        {
            bool loc__0;
    loc__0 = (bool)___ivy_choose(0,"loc:0",232);
            {
                ___ivy_stack.push_back(698);
                loc__0 = selector__execute_r11();
                ___ivy_stack.pop_back();
                if(loc__0){
                    {
                        ___ivy_stack.push_back(699);
                        inspector__check_guard_r11(protocol__r_CodY, protocol__r_hag);
                        ___ivy_stack.pop_back();
                        ___ivy_stack.push_back(700);
                        protocol__r_CodY = updater__decr(protocol__r_CodY);
                        ___ivy_stack.pop_back();
                        ___ivy_stack.push_back(701);
                        protocol__r_hag = updater__decr(protocol__r_hag);
                        ___ivy_stack.pop_back();
                        ___ivy_stack.push_back(702);
                        protocol__r_CodY_hag = updater__incr(protocol__r_CodY_hag);
                        ___ivy_stack.pop_back();
                    }
                }
            }
        }
    }
}
void test_v2::ext__protocol__update_r10(){
    {
        ivy_assume((protocol__idle == (0 & 1)), "test_v2.ivy: line 1064");
        {
            bool loc__0;
    loc__0 = (bool)___ivy_choose(0,"loc:0",233);
            {
                ___ivy_stack.push_back(703);
                loc__0 = enabled_checker__is_enabled_r10(protocol__r_CodY_flache);
                ___ivy_stack.pop_back();
                ivy_assume(loc__0, "test_v2.ivy: line 1065");
            }
        }
        {
            bool loc__0;
    loc__0 = (bool)___ivy_choose(0,"loc:0",234);
            {
                ___ivy_stack.push_back(704);
                loc__0 = selector__execute_r10();
                ___ivy_stack.pop_back();
                if(loc__0){
                    {
                        ___ivy_stack.push_back(705);
                        inspector__check_guard_r10(protocol__r_CodY_flache);
                        ___ivy_stack.pop_back();
                        ___ivy_stack.push_back(706);
                        protocol__r_CodY_flache = updater__decr(protocol__r_CodY_flache);
                        ___ivy_stack.pop_back();
                        ___ivy_stack.push_back(707);
                        protocol__r_CodY = updater__incr(protocol__r_CodY);
                        ___ivy_stack.pop_back();
                        ___ivy_stack.push_back(708);
                        protocol__r_flache = updater__incr(protocol__r_flache);
                        ___ivy_stack.pop_back();
                    }
                }
            }
        }
    }
}
bool test_v2::enabled_checker__is_enabled_r5(unsigned reactant1){
    bool y;
    y = (bool)___ivy_choose(0,"fml:y",0);
    if((((1 & 1023) < reactant1) || (reactant1 == (1 & 1023)))){
        y = true;
    }
    else {
        y = false;
    }
    return y;
}
void test_v2::ext__protocol__update_r12(){
    {
        ivy_assume((protocol__idle == (0 & 1)), "test_v2.ivy: line 1072");
        {
            bool loc__0;
    loc__0 = (bool)___ivy_choose(0,"loc:0",235);
            {
                ___ivy_stack.push_back(709);
                loc__0 = enabled_checker__is_enabled_r12(protocol__r_CodY_hag);
                ___ivy_stack.pop_back();
                ivy_assume(loc__0, "test_v2.ivy: line 1073");
            }
        }
        {
            bool loc__0;
    loc__0 = (bool)___ivy_choose(0,"loc:0",236);
            {
                ___ivy_stack.push_back(710);
                loc__0 = selector__execute_r12();
                ___ivy_stack.pop_back();
                if(loc__0){
                    {
                        ___ivy_stack.push_back(711);
                        inspector__check_guard_r12(protocol__r_CodY_hag);
                        ___ivy_stack.pop_back();
                        ___ivy_stack.push_back(712);
                        protocol__r_CodY_hag = updater__decr(protocol__r_CodY_hag);
                        ___ivy_stack.pop_back();
                        ___ivy_stack.push_back(713);
                        protocol__r_CodY = updater__incr(protocol__r_CodY);
                        ___ivy_stack.pop_back();
                        ___ivy_stack.push_back(714);
                        protocol__r_hag = updater__incr(protocol__r_hag);
                        ___ivy_stack.pop_back();
                    }
                }
            }
        }
    }
}
bool test_v2::selector__execute_r1(){
    bool y;
    y = (bool)___ivy_choose(0,"fml:y",0);
    {
        selector__r1_exec = ((selector__r1_exec + (1 & 255)) & 255);
        if(((selector__r1_rate < selector__r1_exec) || (selector__r1_exec == selector__r1_rate))){
            {
                y = true;
                selector__r1_exec = (0 & 255);
                selector__r1_count = ((selector__r1_count + (1 & 255)) & 255);
            }
        }
        else {
            y = false;
        }
        if(((selector__r1_count_rate < selector__r1_count) || (selector__r1_count == selector__r1_count_rate))){
            {
                selector__r1_stage = ((selector__r1_stage + (1 & 7)) & 7);
                selector__r1_count = (0 & 255);
                selector__r1_count_rate = (4 & 255);
                selector__r1_rate = (1 & 255);
            }
        }
        else {
            if((selector__r1_stage == (1 & 7))){
                {
                    selector__r1_count_rate = (3 & 255);
                    selector__r1_rate = (1 & 255);
                }
            }
            else {
                if((selector__r1_stage == (2 & 7))){
                    {
                        selector__r1_count_rate = (5 & 255);
                        selector__r1_rate = (1 & 255);
                    }
                }
                else {
                    if((selector__r1_stage == (3 & 7))){
                        {
                            selector__r1_count_rate = (4 & 255);
                            selector__r1_rate = (1 & 255);
                        }
                    }
                    else {
                        if((selector__r1_stage == (4 & 7))){
                            {
                                selector__r1_count_rate = (4 & 255);
                                selector__r1_rate = (1 & 255);
                            }
                        }
                        else {
                            if((selector__r1_stage == (5 & 7))){
                                {
                                    selector__r1_count_rate = (5 & 255);
                                    selector__r1_rate = (1 & 255);
                                }
                            }
                            else {
                                if((selector__r1_stage == (6 & 7))){
                                    {
                                        selector__r1_count_rate = (3 & 255);
                                        selector__r1_rate = (1 & 255);
                                    }
                                }
                                else {
                                    if((selector__r1_stage == (7 & 7))){
                                        {
                                            selector__r1_count_rate = (4 & 255);
                                            selector__r1_rate = (1 & 255);
                                        }
                                    }
                                    else {
                                        selector__r1_stage = (0 & 7);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return y;
}
void test_v2::__init(){
    {
        {
            selector__r1_count = (0 & 255);
            selector__r2_count = (0 & 255);
            selector__r3_count = (0 & 255);
            selector__r4_count = (0 & 255);
            selector__r5_count = (0 & 255);
            selector__r6_count = (0 & 255);
            selector__r7_count = (0 & 255);
            selector__r8_count = (0 & 255);
            selector__r9_count = (0 & 255);
            selector__r10_count = (0 & 255);
            selector__r11_count = (0 & 255);
            selector__r12_count = (0 & 255);
            selector__r1_count_rate = (4 & 255);
            selector__r2_count_rate = (4 & 255);
            selector__r3_count_rate = (4 & 255);
            selector__r4_count_rate = (4 & 255);
            selector__r5_count_rate = (4 & 255);
            selector__r6_count_rate = (4 & 255);
            selector__r7_count_rate = (4 & 255);
            selector__r8_count_rate = (4 & 255);
            selector__r9_count_rate = (4 & 255);
            selector__r10_count_rate = (4 & 255);
            selector__r11_count_rate = (4 & 255);
            selector__r12_count_rate = (4 & 255);
        }
        {
            protocol__r_codY = (1 & 1023);
            protocol__r_flache = (1 & 1023);
            protocol__r_SigD_hag = (1 & 1023);
            protocol__r_hag = (1 & 1023);
            protocol__r_CodY_flache = (1 & 1023);
            protocol__r_CodY_hag = (1 & 1023);
            protocol__r_CodY = (11 & 1023);
            protocol__r_SigD = (10 & 1023);
            protocol__r_Hag = (10 & 1023);
            protocol__idle = (0 & 1);
        }
    }
}
bool test_v2::selector__execute_r6(){
    bool y;
    y = (bool)___ivy_choose(0,"fml:y",0);
    {
        selector__r6_exec = ((selector__r6_exec + (1 & 255)) & 255);
        if(((selector__r6_rate < selector__r6_exec) || (selector__r6_exec == selector__r6_rate))){
            {
                y = true;
                selector__r6_exec = (0 & 255);
                selector__r6_count = ((selector__r6_count + (1 & 255)) & 255);
            }
        }
        else {
            y = false;
        }
        if(((selector__r6_count_rate < selector__r6_count) || (selector__r6_count == selector__r6_count_rate))){
            {
                selector__r6_stage = ((selector__r6_stage + (1 & 7)) & 7);
                selector__r6_count = (0 & 255);
                selector__r6_count_rate = (4 & 255);
                selector__r6_rate = (39 & 255);
            }
        }
        else {
            if((selector__r6_stage == (1 & 7))){
                {
                    selector__r6_count_rate = (3 & 255);
                    selector__r6_rate = (39 & 255);
                }
            }
            else {
                if((selector__r6_stage == (2 & 7))){
                    {
                        selector__r6_count_rate = (5 & 255);
                        selector__r6_rate = (39 & 255);
                    }
                }
                else {
                    if((selector__r6_stage == (3 & 7))){
                        {
                            selector__r6_count_rate = (4 & 255);
                            selector__r6_rate = (39 & 255);
                        }
                    }
                    else {
                        if((selector__r6_stage == (4 & 7))){
                            {
                                selector__r6_count_rate = (4 & 255);
                                selector__r6_rate = (39 & 255);
                            }
                        }
                        else {
                            if((selector__r6_stage == (5 & 7))){
                                {
                                    selector__r6_count_rate = (5 & 255);
                                    selector__r6_rate = (39 & 255);
                                }
                            }
                            else {
                                if((selector__r6_stage == (6 & 7))){
                                    {
                                        selector__r6_count_rate = (3 & 255);
                                        selector__r6_rate = (39 & 255);
                                    }
                                }
                                else {
                                    if((selector__r6_stage == (7 & 7))){
                                        {
                                            selector__r6_count_rate = (4 & 255);
                                            selector__r6_rate = (39 & 255);
                                        }
                                    }
                                    else {
                                        selector__r6_stage = (0 & 7);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return y;
}
void test_v2::inspector__check_guard_r12(unsigned reactant1){
    __ivy_out  << "< inspector.check_guard_r12" << "(" << reactant1 << ")" << std::endl;
    {
        ivy_assert((((1 & 1023) < reactant1) || (reactant1 == (1 & 1023))), "test_v2.ivy: line 202");
        ___ivy_stack.push_back(732);
        imp__inspector__check_guard_r12(reactant1);
        ___ivy_stack.pop_back();
    }
}
void test_v2::inspector__check_guard_r10(unsigned reactant1){
    __ivy_out  << "< inspector.check_guard_r10" << "(" << reactant1 << ")" << std::endl;
    {
        ivy_assert((((1 & 1023) < reactant1) || (reactant1 == (1 & 1023))), "test_v2.ivy: line 192");
        ___ivy_stack.push_back(733);
        imp__inspector__check_guard_r10(reactant1);
        ___ivy_stack.pop_back();
    }
}
bool test_v2::selector__execute_r7(){
    bool y;
    y = (bool)___ivy_choose(0,"fml:y",0);
    {
        selector__r7_exec = ((selector__r7_exec + (1 & 255)) & 255);
        if(((selector__r7_rate < selector__r7_exec) || (selector__r7_exec == selector__r7_rate))){
            {
                y = true;
                selector__r7_exec = (0 & 255);
                selector__r7_count = ((selector__r7_count + (1 & 255)) & 255);
            }
        }
        else {
            y = false;
        }
        if(((selector__r7_count_rate < selector__r7_count) || (selector__r7_count == selector__r7_count_rate))){
            {
                selector__r7_stage = ((selector__r7_stage + (1 & 7)) & 7);
                selector__r7_count = (0 & 255);
                selector__r7_count_rate = (4 & 255);
                selector__r7_rate = (39 & 255);
            }
        }
        else {
            if((selector__r7_stage == (1 & 7))){
                {
                    selector__r7_count_rate = (3 & 255);
                    selector__r7_rate = (39 & 255);
                }
            }
            else {
                if((selector__r7_stage == (2 & 7))){
                    {
                        selector__r7_count_rate = (5 & 255);
                        selector__r7_rate = (39 & 255);
                    }
                }
                else {
                    if((selector__r7_stage == (3 & 7))){
                        {
                            selector__r7_count_rate = (4 & 255);
                            selector__r7_rate = (39 & 255);
                        }
                    }
                    else {
                        if((selector__r7_stage == (4 & 7))){
                            {
                                selector__r7_count_rate = (4 & 255);
                                selector__r7_rate = (39 & 255);
                            }
                        }
                        else {
                            if((selector__r7_stage == (5 & 7))){
                                {
                                    selector__r7_count_rate = (5 & 255);
                                    selector__r7_rate = (39 & 255);
                                }
                            }
                            else {
                                if((selector__r7_stage == (6 & 7))){
                                    {
                                        selector__r7_count_rate = (3 & 255);
                                        selector__r7_rate = (39 & 255);
                                    }
                                }
                                else {
                                    if((selector__r7_stage == (7 & 7))){
                                        {
                                            selector__r7_count_rate = (4 & 255);
                                            selector__r7_rate = (39 & 255);
                                        }
                                    }
                                    else {
                                        selector__r7_stage = (0 & 7);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return y;
}
void test_v2::imp__goal__achieved(unsigned v){
    {
    }
}
void test_v2::imp__inspector__check_guard_r11(unsigned reactant1, unsigned reactant2){
    {
    }
}
void test_v2::ext__protocol__update_r7(){
    {
        ivy_assume((protocol__idle == (0 & 1)), "test_v2.ivy: line 1052");
        {
            bool loc__0;
    loc__0 = (bool)___ivy_choose(0,"loc:0",227);
            {
                ___ivy_stack.push_back(687);
                loc__0 = enabled_checker__is_enabled_r7(protocol__r_SigD, protocol__r_hag);
                ___ivy_stack.pop_back();
                ivy_assume(loc__0, "test_v2.ivy: line 1053");
            }
        }
        {
            bool loc__0;
    loc__0 = (bool)___ivy_choose(0,"loc:0",228);
            {
                ___ivy_stack.push_back(688);
                loc__0 = selector__execute_r7();
                ___ivy_stack.pop_back();
                if(loc__0){
                    {
                        ___ivy_stack.push_back(689);
                        inspector__check_guard_r7(protocol__r_SigD, protocol__r_hag);
                        ___ivy_stack.pop_back();
                        ___ivy_stack.push_back(690);
                        protocol__r_SigD = updater__decr(protocol__r_SigD);
                        ___ivy_stack.pop_back();
                        ___ivy_stack.push_back(691);
                        protocol__r_hag = updater__decr(protocol__r_hag);
                        ___ivy_stack.pop_back();
                        ___ivy_stack.push_back(692);
                        protocol__r_SigD_hag = updater__incr(protocol__r_SigD_hag);
                        ___ivy_stack.pop_back();
                    }
                }
            }
        }
    }
}
void test_v2::imp__inspector__check_guard_r12(unsigned reactant1){
    {
    }
}
void test_v2::ext__protocol__update_r6(){
    {
        ivy_assume((protocol__idle == (0 & 1)), "test_v2.ivy: line 1048");
        {
            bool loc__0;
    loc__0 = (bool)___ivy_choose(0,"loc:0",229);
            {
                ___ivy_stack.push_back(693);
                loc__0 = enabled_checker__is_enabled_r6(protocol__r_Hag);
                ___ivy_stack.pop_back();
                ivy_assume(loc__0, "test_v2.ivy: line 1049");
            }
        }
        {
            bool loc__0;
    loc__0 = (bool)___ivy_choose(0,"loc:0",230);
            {
                ___ivy_stack.push_back(694);
                loc__0 = selector__execute_r6();
                ___ivy_stack.pop_back();
                if(loc__0){
                    {
                        ___ivy_stack.push_back(695);
                        inspector__check_guard_r6(protocol__r_Hag);
                        ___ivy_stack.pop_back();
                        ___ivy_stack.push_back(696);
                        protocol__r_Hag = updater__decr(protocol__r_Hag);
                        ___ivy_stack.pop_back();
                    }
                }
            }
        }
    }
}
void test_v2::inspector__check_guard_r11(unsigned reactant1, unsigned reactant2){
    __ivy_out  << "< inspector.check_guard_r11" << "(" << reactant1 << "," << reactant2 << ")" << std::endl;
    {
        ivy_assert(((((1 & 1023) < reactant1) || (reactant1 == (1 & 1023))) && (((1 & 1023) < reactant2) || (reactant2 == (1 & 1023)))), "test_v2.ivy: line 197");
        ___ivy_stack.push_back(738);
        imp__inspector__check_guard_r11(reactant1, reactant2);
        ___ivy_stack.pop_back();
    }
}
unsigned test_v2::updater__incr(unsigned x){
    unsigned y;
    y = (unsigned)___ivy_choose(0,"fml:y",0);
    y = ((x + (1 & 1023)) & 1023);
    return y;
}
unsigned test_v2::updater__decr(unsigned x){
    unsigned y;
    y = (unsigned)___ivy_choose(0,"fml:y",0);
    y = ((x - (1 & 1023)) & 1023);
    return y;
}
void test_v2::imp__inspector__check_guard_r5(unsigned reactant1){
    {
    }
}
void test_v2::imp__inspector__check_guard_r4(unsigned reactant1){
    {
    }
}
void test_v2::imp__inspector__check_guard_r7(unsigned reactant1, unsigned reactant2){
    {
    }
}
void test_v2::imp__inspector__check_guard_r6(unsigned reactant1){
    {
    }
}
void test_v2::imp__inspector__check_guard_r1(unsigned reactant1){
    {
    }
}
void test_v2::imp__inspector__check_guard_r3(unsigned reactant1){
    {
    }
}
void test_v2::imp__inspector__check_guard_r2(unsigned reactant1){
    {
    }
}
void test_v2::imp__inspector__check_guard_r9(unsigned reactant1, unsigned reactant2){
    {
    }
}
void test_v2::imp__inspector__check_guard_r8(unsigned reactant1){
    {
    }
}
void test_v2::imp__inspector__check_guard_r10(unsigned reactant1){
    {
    }
}
void test_v2::__tick(int __timeout){
}
test_v2::test_v2(){
#ifdef _WIN32
mutex = CreateMutex(NULL,FALSE,NULL);
#else
pthread_mutex_init(&mutex,NULL);
#endif
__lock();
    __CARD__protocol__2bit = 2;
    __CARD__updater__exec_stage = 8;
    __CARD__updater__num = 1024;
    __CARD__updater__exec_var = 256;
}
test_v2::~test_v2(){
    __lock(); // otherwise, thread may die holding lock!
    for (unsigned i = 0; i < thread_ids.size(); i++){
#ifdef _WIN32
       // No idea how to cancel a thread on Windows. We just suspend it
       // so it can't cause any harm as we destruct this object.
       SuspendThread(thread_ids[i]);
#else
        pthread_cancel(thread_ids[i]);
        pthread_join(thread_ids[i],NULL);
#endif
    }
    __unlock();
}

class init_gen : public gen {
public:
    init_gen();
    bool generate(test_v2&);
    void execute(test_v2&){}
};
init_gen::init_gen(){
    mk_bv("protocol.2bit",1);
    mk_bv("updater.exec_stage",3);
    mk_bv("updater.num",10);
    mk_bv("updater.exec_var",8);
    mk_const("selector.r12_rate","updater.exec_var");
    mk_const("protocol.r_SigD_hag","updater.num");
    mk_const("protocol.r_CodY_flache","updater.num");
    mk_const("selector.r12_count_rate","updater.exec_var");
    mk_const("selector.r6_exec","updater.exec_var");
    mk_const("selector.r12_exec","updater.exec_var");
    mk_const("selector.r1_exec","updater.exec_var");
    mk_const("selector.r1_count_rate","updater.exec_var");
    mk_const("selector.r8_count","updater.exec_var");
    mk_const("selector.r5_count_rate","updater.exec_var");
    mk_const("selector.r6_rate","updater.exec_var");
    mk_const("selector.r1_count","updater.exec_var");
    mk_const("selector.r2_exec","updater.exec_var");
    mk_const("protocol.r_SigD","updater.num");
    mk_const("protocol.r_hag","updater.num");
    mk_const("selector.r5_exec","updater.exec_var");
    mk_const("selector.r2_count","updater.exec_var");
    mk_const("protocol.r_codY","updater.num");
    mk_const("selector.r10_exec","updater.exec_var");
    mk_const("selector.r4_stage","updater.exec_stage");
    mk_const("selector.r3_count","updater.exec_var");
    mk_const("selector.r7_count_rate","updater.exec_var");
    mk_const("selector.r2_count_rate","updater.exec_var");
    mk_const("selector.r8_exec","updater.exec_var");
    mk_const("protocol.r_CodY_hag","updater.num");
    mk_const("selector.r7_stage","updater.exec_stage");
    mk_const("selector.r12_count","updater.exec_var");
    mk_const("protocol.r_CodY","updater.num");
    mk_const("selector.r10_stage","updater.exec_stage");
    mk_const("selector.r9_count","updater.exec_var");
    mk_const("selector.r4_count","updater.exec_var");
    mk_const("selector.r7_count","updater.exec_var");
    mk_const("selector.r11_count_rate","updater.exec_var");
    mk_const("selector.r2_rate","updater.exec_var");
    mk_const("selector.r1_rate","updater.exec_var");
    mk_const("selector.r4_exec","updater.exec_var");
    mk_const("selector.r12_stage","updater.exec_stage");
    mk_const("selector.r11_rate","updater.exec_var");
    mk_const("selector.r2_stage","updater.exec_stage");
    mk_const("protocol.r_flache","updater.num");
    mk_const("selector.r10_rate","updater.exec_var");
    mk_const("selector.r5_stage","updater.exec_stage");
    mk_const("selector.r11_exec","updater.exec_var");
    mk_const("selector.r8_stage","updater.exec_stage");
    mk_const("selector.r4_count_rate","updater.exec_var");
    mk_const("selector.r3_count_rate","updater.exec_var");
    mk_const("_generating","Bool");
    mk_const("selector.r4_rate","updater.exec_var");
    mk_const("selector.r11_count","updater.exec_var");
    mk_const("selector.r10_count","updater.exec_var");
    mk_const("selector.r9_count_rate","updater.exec_var");
    mk_const("selector.r6_stage","updater.exec_stage");
    mk_const("selector.r7_exec","updater.exec_var");
    mk_const("selector.r9_stage","updater.exec_stage");
    mk_const("selector.r5_count","updater.exec_var");
    mk_const("selector.r8_rate","updater.exec_var");
    mk_const("selector.r6_count","updater.exec_var");
    mk_const("protocol.r_Hag","updater.num");
    mk_const("selector.r5_rate","updater.exec_var");
    mk_const("selector.r6_count_rate","updater.exec_var");
    mk_const("selector.r3_rate","updater.exec_var");
    mk_const("selector.r3_stage","updater.exec_stage");
    mk_const("protocol.idle","protocol.2bit");
    mk_const("selector.r9_exec","updater.exec_var");
    mk_const("selector.r3_exec","updater.exec_var");
    mk_const("selector.r9_rate","updater.exec_var");
    mk_const("selector.r10_count_rate","updater.exec_var");
    mk_const("selector.r7_rate","updater.exec_var");
    mk_const("selector.r1_stage","updater.exec_stage");
    mk_const("selector.r8_count_rate","updater.exec_var");
    mk_const("selector.r11_stage","updater.exec_stage");
    add("(assert (and\
      and\
    ))");
}
bool init_gen::generate(test_v2& obj) {
    alits.clear();
    obj.selector__r12_rate = (unsigned)(rand() % 256);
    obj.protocol__r_SigD_hag = (unsigned)(rand() % 1024);
    obj.protocol__r_CodY_flache = (unsigned)(rand() % 1024);
    obj.selector__r12_count_rate = (unsigned)(rand() % 256);
    obj.selector__r6_exec = (unsigned)(rand() % 256);
    obj.selector__r12_exec = (unsigned)(rand() % 256);
    obj.selector__r1_exec = (unsigned)(rand() % 256);
    obj.selector__r1_count_rate = (unsigned)(rand() % 256);
    obj.selector__r8_count = (unsigned)(rand() % 256);
    obj.selector__r5_count_rate = (unsigned)(rand() % 256);
    obj.selector__r6_rate = (unsigned)(rand() % 256);
    obj.selector__r1_count = (unsigned)(rand() % 256);
    obj.selector__r2_exec = (unsigned)(rand() % 256);
    obj.protocol__r_SigD = (unsigned)(rand() % 1024);
    obj.protocol__r_hag = (unsigned)(rand() % 1024);
    obj.selector__r5_exec = (unsigned)(rand() % 256);
    obj.selector__r2_count = (unsigned)(rand() % 256);
    obj.protocol__r_codY = (unsigned)(rand() % 1024);
    obj.selector__r10_exec = (unsigned)(rand() % 256);
    obj.selector__r4_stage = (unsigned)(rand() % 8);
    obj.selector__r3_count = (unsigned)(rand() % 256);
    obj.selector__r7_count_rate = (unsigned)(rand() % 256);
    obj.selector__r2_count_rate = (unsigned)(rand() % 256);
    obj.selector__r8_exec = (unsigned)(rand() % 256);
    obj.protocol__r_CodY_hag = (unsigned)(rand() % 1024);
    obj.selector__r7_stage = (unsigned)(rand() % 8);
    obj.selector__r12_count = (unsigned)(rand() % 256);
    obj.protocol__r_CodY = (unsigned)(rand() % 1024);
    obj.selector__r10_stage = (unsigned)(rand() % 8);
    obj.selector__r9_count = (unsigned)(rand() % 256);
    obj.selector__r4_count = (unsigned)(rand() % 256);
    obj.selector__r7_count = (unsigned)(rand() % 256);
    obj.selector__r11_count_rate = (unsigned)(rand() % 256);
    obj.selector__r2_rate = (unsigned)(rand() % 256);
    obj.selector__r1_rate = (unsigned)(rand() % 256);
    obj.selector__r4_exec = (unsigned)(rand() % 256);
    obj.selector__r12_stage = (unsigned)(rand() % 8);
    obj.selector__r11_rate = (unsigned)(rand() % 256);
    obj.selector__r2_stage = (unsigned)(rand() % 8);
    obj.protocol__r_flache = (unsigned)(rand() % 1024);
    obj.selector__r10_rate = (unsigned)(rand() % 256);
    obj.selector__r5_stage = (unsigned)(rand() % 8);
    obj.selector__r11_exec = (unsigned)(rand() % 256);
    obj.selector__r8_stage = (unsigned)(rand() % 8);
    obj.selector__r4_count_rate = (unsigned)(rand() % 256);
    obj.selector__r3_count_rate = (unsigned)(rand() % 256);
    obj._generating = (bool)(rand() % 2);
    obj.selector__r4_rate = (unsigned)(rand() % 256);
    obj.selector__r11_count = (unsigned)(rand() % 256);
    obj.selector__r10_count = (unsigned)(rand() % 256);
    obj.selector__r9_count_rate = (unsigned)(rand() % 256);
    obj.selector__r6_stage = (unsigned)(rand() % 8);
    obj.selector__r7_exec = (unsigned)(rand() % 256);
    obj.selector__r9_stage = (unsigned)(rand() % 8);
    obj.selector__r5_count = (unsigned)(rand() % 256);
    obj.selector__r8_rate = (unsigned)(rand() % 256);
    obj.selector__r6_count = (unsigned)(rand() % 256);
    obj.protocol__r_Hag = (unsigned)(rand() % 1024);
    obj.selector__r5_rate = (unsigned)(rand() % 256);
    obj.selector__r6_count_rate = (unsigned)(rand() % 256);
    obj.selector__r3_rate = (unsigned)(rand() % 256);
    obj.selector__r3_stage = (unsigned)(rand() % 8);
    obj.protocol__idle = (unsigned)(rand() % 2);
    obj.selector__r9_exec = (unsigned)(rand() % 256);
    obj.selector__r3_exec = (unsigned)(rand() % 256);
    obj.selector__r9_rate = (unsigned)(rand() % 256);
    obj.selector__r10_count_rate = (unsigned)(rand() % 256);
    obj.selector__r7_rate = (unsigned)(rand() % 256);
    obj.selector__r1_stage = (unsigned)(rand() % 8);
    obj.selector__r8_count_rate = (unsigned)(rand() % 256);
    obj.selector__r11_stage = (unsigned)(rand() % 8);

    // std::cout << slvr << std::endl;
    bool __res = solve();
    if (__res) {

    }

    obj.___ivy_gen = this;
    obj.__init();
    return __res;
}
class ext__protocol__idling_gen : public gen {
  public:
    ext__protocol__idling_gen();
    bool generate(test_v2&);
    void execute(test_v2&);
};
ext__protocol__idling_gen::ext__protocol__idling_gen(){
    mk_bv("protocol.2bit",1);
    mk_bv("updater.exec_stage",3);
    mk_bv("updater.num",10);
    mk_bv("updater.exec_var",8);
    mk_const("selector.r12_rate","updater.exec_var");
    mk_const("protocol.r_SigD_hag","updater.num");
    mk_const("protocol.r_CodY_flache","updater.num");
    mk_const("selector.r12_count_rate","updater.exec_var");
    mk_const("selector.r6_exec","updater.exec_var");
    mk_const("selector.r12_exec","updater.exec_var");
    mk_const("selector.r1_exec","updater.exec_var");
    mk_const("selector.r1_count_rate","updater.exec_var");
    mk_const("selector.r8_count","updater.exec_var");
    mk_const("selector.r5_count_rate","updater.exec_var");
    mk_const("selector.r6_rate","updater.exec_var");
    mk_const("selector.r1_count","updater.exec_var");
    mk_const("selector.r2_exec","updater.exec_var");
    mk_const("protocol.r_SigD","updater.num");
    mk_const("protocol.r_hag","updater.num");
    mk_const("selector.r5_exec","updater.exec_var");
    mk_const("selector.r2_count","updater.exec_var");
    mk_const("protocol.r_codY","updater.num");
    mk_const("selector.r10_exec","updater.exec_var");
    mk_const("selector.r4_stage","updater.exec_stage");
    mk_const("selector.r3_count","updater.exec_var");
    mk_const("selector.r7_count_rate","updater.exec_var");
    mk_const("selector.r2_count_rate","updater.exec_var");
    mk_const("selector.r8_exec","updater.exec_var");
    mk_const("protocol.r_CodY_hag","updater.num");
    mk_const("selector.r7_stage","updater.exec_stage");
    mk_const("selector.r12_count","updater.exec_var");
    mk_const("protocol.r_CodY","updater.num");
    mk_const("selector.r10_stage","updater.exec_stage");
    mk_const("selector.r9_count","updater.exec_var");
    mk_const("selector.r4_count","updater.exec_var");
    mk_const("selector.r7_count","updater.exec_var");
    mk_const("selector.r11_count_rate","updater.exec_var");
    mk_const("selector.r2_rate","updater.exec_var");
    mk_const("selector.r1_rate","updater.exec_var");
    mk_const("selector.r4_exec","updater.exec_var");
    mk_const("selector.r12_stage","updater.exec_stage");
    mk_const("selector.r11_rate","updater.exec_var");
    mk_const("selector.r2_stage","updater.exec_stage");
    mk_const("protocol.r_flache","updater.num");
    mk_const("selector.r10_rate","updater.exec_var");
    mk_const("selector.r5_stage","updater.exec_stage");
    mk_const("selector.r11_exec","updater.exec_var");
    mk_const("selector.r8_stage","updater.exec_stage");
    mk_const("selector.r4_count_rate","updater.exec_var");
    mk_const("selector.r3_count_rate","updater.exec_var");
    mk_const("_generating","Bool");
    mk_const("selector.r4_rate","updater.exec_var");
    mk_const("selector.r11_count","updater.exec_var");
    mk_const("selector.r10_count","updater.exec_var");
    mk_const("selector.r9_count_rate","updater.exec_var");
    mk_const("selector.r6_stage","updater.exec_stage");
    mk_const("selector.r7_exec","updater.exec_var");
    mk_const("selector.r9_stage","updater.exec_stage");
    mk_const("selector.r5_count","updater.exec_var");
    mk_const("selector.r8_rate","updater.exec_var");
    mk_const("selector.r6_count","updater.exec_var");
    mk_const("protocol.r_Hag","updater.num");
    mk_const("selector.r5_rate","updater.exec_var");
    mk_const("selector.r6_count_rate","updater.exec_var");
    mk_const("selector.r3_rate","updater.exec_var");
    mk_const("selector.r3_stage","updater.exec_stage");
    mk_const("protocol.idle","protocol.2bit");
    mk_const("selector.r9_exec","updater.exec_var");
    mk_const("selector.r3_exec","updater.exec_var");
    mk_const("selector.r9_rate","updater.exec_var");
    mk_const("selector.r10_count_rate","updater.exec_var");
    mk_const("selector.r7_rate","updater.exec_var");
    mk_const("selector.r1_stage","updater.exec_stage");
    mk_const("selector.r8_count_rate","updater.exec_var");
    mk_const("selector.r11_stage","updater.exec_stage");
    add("(assert (and (= protocol.idle #b1)))");
}
bool ext__protocol__idling_gen::generate(test_v2& obj) {
    push();
    slvr.add(__to_solver(*this,apply("protocol.idle"),obj.protocol__idle));
    alits.clear();

    // std::cout << slvr << std::endl;
    bool __res = solve();
    if (__res) {

    }
    pop();
    obj.___ivy_gen = this;
    return __res;
}
void ext__protocol__idling_gen::execute(test_v2& obj){
    __ivy_out << "> protocol.idling" << std::endl;
    obj.ext__protocol__idling();
}
class ext__protocol__fail_test_gen : public gen {
  public:
    ext__protocol__fail_test_gen();
    bool generate(test_v2&);
    void execute(test_v2&);
};
ext__protocol__fail_test_gen::ext__protocol__fail_test_gen(){
    mk_bv("protocol.2bit",1);
    mk_bv("updater.exec_stage",3);
    mk_bv("updater.num",10);
    mk_bv("updater.exec_var",8);
    mk_const("selector.r12_rate","updater.exec_var");
    mk_const("protocol.r_SigD_hag","updater.num");
    mk_const("protocol.r_CodY_flache","updater.num");
    mk_const("selector.r12_count_rate","updater.exec_var");
    mk_const("selector.r6_exec","updater.exec_var");
    mk_const("selector.r12_exec","updater.exec_var");
    mk_const("selector.r1_exec","updater.exec_var");
    mk_const("selector.r1_count_rate","updater.exec_var");
    mk_const("selector.r8_count","updater.exec_var");
    mk_const("selector.r5_count_rate","updater.exec_var");
    mk_const("selector.r6_rate","updater.exec_var");
    mk_const("selector.r1_count","updater.exec_var");
    mk_const("selector.r2_exec","updater.exec_var");
    mk_const("protocol.r_SigD","updater.num");
    mk_const("protocol.r_hag","updater.num");
    mk_const("selector.r5_exec","updater.exec_var");
    mk_const("selector.r2_count","updater.exec_var");
    mk_const("protocol.r_codY","updater.num");
    mk_const("selector.r10_exec","updater.exec_var");
    mk_const("selector.r4_stage","updater.exec_stage");
    mk_const("selector.r3_count","updater.exec_var");
    mk_const("selector.r7_count_rate","updater.exec_var");
    mk_const("selector.r2_count_rate","updater.exec_var");
    mk_const("selector.r8_exec","updater.exec_var");
    mk_const("protocol.r_CodY_hag","updater.num");
    mk_const("selector.r7_stage","updater.exec_stage");
    mk_const("selector.r12_count","updater.exec_var");
    mk_const("protocol.r_CodY","updater.num");
    mk_const("selector.r10_stage","updater.exec_stage");
    mk_const("selector.r9_count","updater.exec_var");
    mk_const("selector.r4_count","updater.exec_var");
    mk_const("selector.r7_count","updater.exec_var");
    mk_const("selector.r11_count_rate","updater.exec_var");
    mk_const("selector.r2_rate","updater.exec_var");
    mk_const("selector.r1_rate","updater.exec_var");
    mk_const("selector.r4_exec","updater.exec_var");
    mk_const("selector.r12_stage","updater.exec_stage");
    mk_const("selector.r11_rate","updater.exec_var");
    mk_const("selector.r2_stage","updater.exec_stage");
    mk_const("protocol.r_flache","updater.num");
    mk_const("selector.r10_rate","updater.exec_var");
    mk_const("selector.r5_stage","updater.exec_stage");
    mk_const("selector.r11_exec","updater.exec_var");
    mk_const("selector.r8_stage","updater.exec_stage");
    mk_const("selector.r4_count_rate","updater.exec_var");
    mk_const("selector.r3_count_rate","updater.exec_var");
    mk_const("_generating","Bool");
    mk_const("selector.r4_rate","updater.exec_var");
    mk_const("selector.r11_count","updater.exec_var");
    mk_const("selector.r10_count","updater.exec_var");
    mk_const("selector.r9_count_rate","updater.exec_var");
    mk_const("selector.r6_stage","updater.exec_stage");
    mk_const("selector.r7_exec","updater.exec_var");
    mk_const("selector.r9_stage","updater.exec_stage");
    mk_const("selector.r5_count","updater.exec_var");
    mk_const("selector.r8_rate","updater.exec_var");
    mk_const("selector.r6_count","updater.exec_var");
    mk_const("protocol.r_Hag","updater.num");
    mk_const("selector.r5_rate","updater.exec_var");
    mk_const("selector.r6_count_rate","updater.exec_var");
    mk_const("selector.r3_rate","updater.exec_var");
    mk_const("selector.r3_stage","updater.exec_stage");
    mk_const("protocol.idle","protocol.2bit");
    mk_const("selector.r9_exec","updater.exec_var");
    mk_const("selector.r3_exec","updater.exec_var");
    mk_const("selector.r9_rate","updater.exec_var");
    mk_const("selector.r10_count_rate","updater.exec_var");
    mk_const("selector.r7_rate","updater.exec_var");
    mk_const("selector.r1_stage","updater.exec_stage");
    mk_const("selector.r8_count_rate","updater.exec_var");
    mk_const("selector.r11_stage","updater.exec_stage");
    mk_const("__ts0_a_f","Bool");
    mk_const("__ts0_a_d","Bool");
    mk_const("__ts0_a_j","Bool");
    mk_const("__ts0_a_h","Bool");
    mk_const("__ts0_a","Bool");
    mk_const("__ts0_a_a","Bool");
    mk_const("__ts0_a_e","Bool");
    mk_const("__ts0_a_c","Bool");
    mk_const("__ts0_a_i","Bool");
    mk_const("__ts0_a_g","Bool");
    mk_const("__ts0_a_k","Bool");
    mk_const("__ts0_a_b","Bool");
    add("(assert (and (= __ts0_a (bvuge protocol.r_codY #b0000000001)) "
"     (= __ts0_a_a (bvuge protocol.r_CodY #b0000000001)) "
"     (= __ts0_a_b (bvuge protocol.r_flache #b0000000001)) "
"     (= __ts0_a_c (bvuge protocol.r_SigD #b0000000001)) "
"     (= __ts0_a_d (bvuge protocol.r_SigD_hag #b0000000001)) "
"     (= __ts0_a_e (bvuge protocol.r_Hag #b0000000001)) "
"     (= __ts0_a_f "
"        (and (bvuge protocol.r_SigD #b0000000001) "
"             (bvuge protocol.r_hag #b0000000001))) "
"     (= __ts0_a_g (bvuge protocol.r_SigD_hag #b0000000001)) "
"     (= __ts0_a_h "
"        (and (bvuge protocol.r_CodY #b0000000001) "
"             (bvuge protocol.r_flache #b0000000001))) "
"     (= __ts0_a_i (bvuge protocol.r_CodY_flache #b0000000001)) "
"     (= __ts0_a_j "
"        (and (bvuge protocol.r_CodY #b0000000001) "
"             (bvuge protocol.r_hag #b0000000001))) "
"     (= __ts0_a_k (bvuge protocol.r_CodY_hag #b0000000001)) "
"     (= protocol.idle #b0) "
"     (= __ts0_a or) "
"     (= __ts0_a_a or) "
"     (= __ts0_a_b or) "
"     (= __ts0_a_c or) "
"     (= __ts0_a_d or) "
"     (= __ts0_a_e or) "
"     (= __ts0_a_f or) "
"     (= __ts0_a_g or) "
"     (= __ts0_a_h or) "
"     (= __ts0_a_i or) "
"     (= __ts0_a_j or) "
"     (= __ts0_a_k or)))");
}
bool ext__protocol__fail_test_gen::generate(test_v2& obj) {
    push();
    slvr.add(__to_solver(*this,apply("protocol.r_SigD_hag"),obj.protocol__r_SigD_hag));
    slvr.add(__to_solver(*this,apply("protocol.r_CodY_flache"),obj.protocol__r_CodY_flache));
    slvr.add(__to_solver(*this,apply("protocol.r_SigD"),obj.protocol__r_SigD));
    slvr.add(__to_solver(*this,apply("protocol.r_hag"),obj.protocol__r_hag));
    slvr.add(__to_solver(*this,apply("protocol.r_codY"),obj.protocol__r_codY));
    slvr.add(__to_solver(*this,apply("protocol.r_CodY_hag"),obj.protocol__r_CodY_hag));
    slvr.add(__to_solver(*this,apply("protocol.r_CodY"),obj.protocol__r_CodY));
    slvr.add(__to_solver(*this,apply("protocol.r_flache"),obj.protocol__r_flache));
    slvr.add(__to_solver(*this,apply("protocol.r_Hag"),obj.protocol__r_Hag));
    slvr.add(__to_solver(*this,apply("protocol.idle"),obj.protocol__idle));
    alits.clear();

    // std::cout << slvr << std::endl;
    bool __res = solve();
    if (__res) {

    }
    pop();
    obj.___ivy_gen = this;
    return __res;
}
void ext__protocol__fail_test_gen::execute(test_v2& obj){
    __ivy_out << "> protocol.fail_test" << std::endl;
    obj.ext__protocol__fail_test();
}
class ext__protocol__update_r9_gen : public gen {
  public:
    ext__protocol__update_r9_gen();
    bool generate(test_v2&);
    void execute(test_v2&);
};
ext__protocol__update_r9_gen::ext__protocol__update_r9_gen(){
    mk_bv("protocol.2bit",1);
    mk_bv("updater.exec_stage",3);
    mk_bv("updater.num",10);
    mk_bv("updater.exec_var",8);
    mk_const("selector.r12_rate","updater.exec_var");
    mk_const("protocol.r_SigD_hag","updater.num");
    mk_const("protocol.r_CodY_flache","updater.num");
    mk_const("selector.r12_count_rate","updater.exec_var");
    mk_const("selector.r6_exec","updater.exec_var");
    mk_const("selector.r12_exec","updater.exec_var");
    mk_const("selector.r1_exec","updater.exec_var");
    mk_const("selector.r1_count_rate","updater.exec_var");
    mk_const("selector.r8_count","updater.exec_var");
    mk_const("selector.r5_count_rate","updater.exec_var");
    mk_const("selector.r6_rate","updater.exec_var");
    mk_const("selector.r1_count","updater.exec_var");
    mk_const("selector.r2_exec","updater.exec_var");
    mk_const("protocol.r_SigD","updater.num");
    mk_const("protocol.r_hag","updater.num");
    mk_const("selector.r5_exec","updater.exec_var");
    mk_const("selector.r2_count","updater.exec_var");
    mk_const("protocol.r_codY","updater.num");
    mk_const("selector.r10_exec","updater.exec_var");
    mk_const("selector.r4_stage","updater.exec_stage");
    mk_const("selector.r3_count","updater.exec_var");
    mk_const("selector.r7_count_rate","updater.exec_var");
    mk_const("selector.r2_count_rate","updater.exec_var");
    mk_const("selector.r8_exec","updater.exec_var");
    mk_const("protocol.r_CodY_hag","updater.num");
    mk_const("selector.r7_stage","updater.exec_stage");
    mk_const("selector.r12_count","updater.exec_var");
    mk_const("protocol.r_CodY","updater.num");
    mk_const("selector.r10_stage","updater.exec_stage");
    mk_const("selector.r9_count","updater.exec_var");
    mk_const("selector.r4_count","updater.exec_var");
    mk_const("selector.r7_count","updater.exec_var");
    mk_const("selector.r11_count_rate","updater.exec_var");
    mk_const("selector.r2_rate","updater.exec_var");
    mk_const("selector.r1_rate","updater.exec_var");
    mk_const("selector.r4_exec","updater.exec_var");
    mk_const("selector.r12_stage","updater.exec_stage");
    mk_const("selector.r11_rate","updater.exec_var");
    mk_const("selector.r2_stage","updater.exec_stage");
    mk_const("protocol.r_flache","updater.num");
    mk_const("selector.r10_rate","updater.exec_var");
    mk_const("selector.r5_stage","updater.exec_stage");
    mk_const("selector.r11_exec","updater.exec_var");
    mk_const("selector.r8_stage","updater.exec_stage");
    mk_const("selector.r4_count_rate","updater.exec_var");
    mk_const("selector.r3_count_rate","updater.exec_var");
    mk_const("_generating","Bool");
    mk_const("selector.r4_rate","updater.exec_var");
    mk_const("selector.r11_count","updater.exec_var");
    mk_const("selector.r10_count","updater.exec_var");
    mk_const("selector.r9_count_rate","updater.exec_var");
    mk_const("selector.r6_stage","updater.exec_stage");
    mk_const("selector.r7_exec","updater.exec_var");
    mk_const("selector.r9_stage","updater.exec_stage");
    mk_const("selector.r5_count","updater.exec_var");
    mk_const("selector.r8_rate","updater.exec_var");
    mk_const("selector.r6_count","updater.exec_var");
    mk_const("protocol.r_Hag","updater.num");
    mk_const("selector.r5_rate","updater.exec_var");
    mk_const("selector.r6_count_rate","updater.exec_var");
    mk_const("selector.r3_rate","updater.exec_var");
    mk_const("selector.r3_stage","updater.exec_stage");
    mk_const("protocol.idle","protocol.2bit");
    mk_const("selector.r9_exec","updater.exec_var");
    mk_const("selector.r3_exec","updater.exec_var");
    mk_const("selector.r9_rate","updater.exec_var");
    mk_const("selector.r10_count_rate","updater.exec_var");
    mk_const("selector.r7_rate","updater.exec_var");
    mk_const("selector.r1_stage","updater.exec_stage");
    mk_const("selector.r8_count_rate","updater.exec_var");
    mk_const("selector.r11_stage","updater.exec_stage");
    mk_const("__ts0_a","Bool");
    add("(assert (and (= __ts0_a "
"        (and (bvuge protocol.r_CodY #b0000000001) "
"             (bvuge protocol.r_flache #b0000000001))) "
"     (= protocol.idle #b0) "
"     __ts0_a))");
}
bool ext__protocol__update_r9_gen::generate(test_v2& obj) {
    push();
    slvr.add(__to_solver(*this,apply("protocol.r_CodY"),obj.protocol__r_CodY));
    slvr.add(__to_solver(*this,apply("protocol.r_flache"),obj.protocol__r_flache));
    slvr.add(__to_solver(*this,apply("protocol.idle"),obj.protocol__idle));
    alits.clear();

    // std::cout << slvr << std::endl;
    bool __res = solve();
    if (__res) {

    }
    pop();
    obj.___ivy_gen = this;
    return __res;
}
void ext__protocol__update_r9_gen::execute(test_v2& obj){
    __ivy_out << "> protocol.update_r9" << std::endl;
    obj.ext__protocol__update_r9();
}
class ext__protocol__update_r8_gen : public gen {
  public:
    ext__protocol__update_r8_gen();
    bool generate(test_v2&);
    void execute(test_v2&);
};
ext__protocol__update_r8_gen::ext__protocol__update_r8_gen(){
    mk_bv("protocol.2bit",1);
    mk_bv("updater.exec_stage",3);
    mk_bv("updater.num",10);
    mk_bv("updater.exec_var",8);
    mk_const("selector.r12_rate","updater.exec_var");
    mk_const("protocol.r_SigD_hag","updater.num");
    mk_const("protocol.r_CodY_flache","updater.num");
    mk_const("selector.r12_count_rate","updater.exec_var");
    mk_const("selector.r6_exec","updater.exec_var");
    mk_const("selector.r12_exec","updater.exec_var");
    mk_const("selector.r1_exec","updater.exec_var");
    mk_const("selector.r1_count_rate","updater.exec_var");
    mk_const("selector.r8_count","updater.exec_var");
    mk_const("selector.r5_count_rate","updater.exec_var");
    mk_const("selector.r6_rate","updater.exec_var");
    mk_const("selector.r1_count","updater.exec_var");
    mk_const("selector.r2_exec","updater.exec_var");
    mk_const("protocol.r_SigD","updater.num");
    mk_const("protocol.r_hag","updater.num");
    mk_const("selector.r5_exec","updater.exec_var");
    mk_const("selector.r2_count","updater.exec_var");
    mk_const("protocol.r_codY","updater.num");
    mk_const("selector.r10_exec","updater.exec_var");
    mk_const("selector.r4_stage","updater.exec_stage");
    mk_const("selector.r3_count","updater.exec_var");
    mk_const("selector.r7_count_rate","updater.exec_var");
    mk_const("selector.r2_count_rate","updater.exec_var");
    mk_const("selector.r8_exec","updater.exec_var");
    mk_const("protocol.r_CodY_hag","updater.num");
    mk_const("selector.r7_stage","updater.exec_stage");
    mk_const("selector.r12_count","updater.exec_var");
    mk_const("protocol.r_CodY","updater.num");
    mk_const("selector.r10_stage","updater.exec_stage");
    mk_const("selector.r9_count","updater.exec_var");
    mk_const("selector.r4_count","updater.exec_var");
    mk_const("selector.r7_count","updater.exec_var");
    mk_const("selector.r11_count_rate","updater.exec_var");
    mk_const("selector.r2_rate","updater.exec_var");
    mk_const("selector.r1_rate","updater.exec_var");
    mk_const("selector.r4_exec","updater.exec_var");
    mk_const("selector.r12_stage","updater.exec_stage");
    mk_const("selector.r11_rate","updater.exec_var");
    mk_const("selector.r2_stage","updater.exec_stage");
    mk_const("protocol.r_flache","updater.num");
    mk_const("selector.r10_rate","updater.exec_var");
    mk_const("selector.r5_stage","updater.exec_stage");
    mk_const("selector.r11_exec","updater.exec_var");
    mk_const("selector.r8_stage","updater.exec_stage");
    mk_const("selector.r4_count_rate","updater.exec_var");
    mk_const("selector.r3_count_rate","updater.exec_var");
    mk_const("_generating","Bool");
    mk_const("selector.r4_rate","updater.exec_var");
    mk_const("selector.r11_count","updater.exec_var");
    mk_const("selector.r10_count","updater.exec_var");
    mk_const("selector.r9_count_rate","updater.exec_var");
    mk_const("selector.r6_stage","updater.exec_stage");
    mk_const("selector.r7_exec","updater.exec_var");
    mk_const("selector.r9_stage","updater.exec_stage");
    mk_const("selector.r5_count","updater.exec_var");
    mk_const("selector.r8_rate","updater.exec_var");
    mk_const("selector.r6_count","updater.exec_var");
    mk_const("protocol.r_Hag","updater.num");
    mk_const("selector.r5_rate","updater.exec_var");
    mk_const("selector.r6_count_rate","updater.exec_var");
    mk_const("selector.r3_rate","updater.exec_var");
    mk_const("selector.r3_stage","updater.exec_stage");
    mk_const("protocol.idle","protocol.2bit");
    mk_const("selector.r9_exec","updater.exec_var");
    mk_const("selector.r3_exec","updater.exec_var");
    mk_const("selector.r9_rate","updater.exec_var");
    mk_const("selector.r10_count_rate","updater.exec_var");
    mk_const("selector.r7_rate","updater.exec_var");
    mk_const("selector.r1_stage","updater.exec_stage");
    mk_const("selector.r8_count_rate","updater.exec_var");
    mk_const("selector.r11_stage","updater.exec_stage");
    mk_const("__ts0_a","Bool");
    add("(assert (and (= __ts0_a (bvuge protocol.r_SigD_hag #b0000000001)) "
"     (= protocol.idle #b0) "
"     __ts0_a))");
}
bool ext__protocol__update_r8_gen::generate(test_v2& obj) {
    push();
    slvr.add(__to_solver(*this,apply("protocol.r_SigD_hag"),obj.protocol__r_SigD_hag));
    slvr.add(__to_solver(*this,apply("protocol.idle"),obj.protocol__idle));
    alits.clear();

    // std::cout << slvr << std::endl;
    bool __res = solve();
    if (__res) {

    }
    pop();
    obj.___ivy_gen = this;
    return __res;
}
void ext__protocol__update_r8_gen::execute(test_v2& obj){
    __ivy_out << "> protocol.update_r8" << std::endl;
    obj.ext__protocol__update_r8();
}
class ext__protocol__update_r1_gen : public gen {
  public:
    ext__protocol__update_r1_gen();
    bool generate(test_v2&);
    void execute(test_v2&);
};
ext__protocol__update_r1_gen::ext__protocol__update_r1_gen(){
    mk_bv("protocol.2bit",1);
    mk_bv("updater.exec_stage",3);
    mk_bv("updater.num",10);
    mk_bv("updater.exec_var",8);
    mk_const("selector.r12_rate","updater.exec_var");
    mk_const("protocol.r_SigD_hag","updater.num");
    mk_const("protocol.r_CodY_flache","updater.num");
    mk_const("selector.r12_count_rate","updater.exec_var");
    mk_const("selector.r6_exec","updater.exec_var");
    mk_const("selector.r12_exec","updater.exec_var");
    mk_const("selector.r1_exec","updater.exec_var");
    mk_const("selector.r1_count_rate","updater.exec_var");
    mk_const("selector.r8_count","updater.exec_var");
    mk_const("selector.r5_count_rate","updater.exec_var");
    mk_const("selector.r6_rate","updater.exec_var");
    mk_const("selector.r1_count","updater.exec_var");
    mk_const("selector.r2_exec","updater.exec_var");
    mk_const("protocol.r_SigD","updater.num");
    mk_const("protocol.r_hag","updater.num");
    mk_const("selector.r5_exec","updater.exec_var");
    mk_const("selector.r2_count","updater.exec_var");
    mk_const("protocol.r_codY","updater.num");
    mk_const("selector.r10_exec","updater.exec_var");
    mk_const("selector.r4_stage","updater.exec_stage");
    mk_const("selector.r3_count","updater.exec_var");
    mk_const("selector.r7_count_rate","updater.exec_var");
    mk_const("selector.r2_count_rate","updater.exec_var");
    mk_const("selector.r8_exec","updater.exec_var");
    mk_const("protocol.r_CodY_hag","updater.num");
    mk_const("selector.r7_stage","updater.exec_stage");
    mk_const("selector.r12_count","updater.exec_var");
    mk_const("protocol.r_CodY","updater.num");
    mk_const("selector.r10_stage","updater.exec_stage");
    mk_const("selector.r9_count","updater.exec_var");
    mk_const("selector.r4_count","updater.exec_var");
    mk_const("selector.r7_count","updater.exec_var");
    mk_const("selector.r11_count_rate","updater.exec_var");
    mk_const("selector.r2_rate","updater.exec_var");
    mk_const("selector.r1_rate","updater.exec_var");
    mk_const("selector.r4_exec","updater.exec_var");
    mk_const("selector.r12_stage","updater.exec_stage");
    mk_const("selector.r11_rate","updater.exec_var");
    mk_const("selector.r2_stage","updater.exec_stage");
    mk_const("protocol.r_flache","updater.num");
    mk_const("selector.r10_rate","updater.exec_var");
    mk_const("selector.r5_stage","updater.exec_stage");
    mk_const("selector.r11_exec","updater.exec_var");
    mk_const("selector.r8_stage","updater.exec_stage");
    mk_const("selector.r4_count_rate","updater.exec_var");
    mk_const("selector.r3_count_rate","updater.exec_var");
    mk_const("_generating","Bool");
    mk_const("selector.r4_rate","updater.exec_var");
    mk_const("selector.r11_count","updater.exec_var");
    mk_const("selector.r10_count","updater.exec_var");
    mk_const("selector.r9_count_rate","updater.exec_var");
    mk_const("selector.r6_stage","updater.exec_stage");
    mk_const("selector.r7_exec","updater.exec_var");
    mk_const("selector.r9_stage","updater.exec_stage");
    mk_const("selector.r5_count","updater.exec_var");
    mk_const("selector.r8_rate","updater.exec_var");
    mk_const("selector.r6_count","updater.exec_var");
    mk_const("protocol.r_Hag","updater.num");
    mk_const("selector.r5_rate","updater.exec_var");
    mk_const("selector.r6_count_rate","updater.exec_var");
    mk_const("selector.r3_rate","updater.exec_var");
    mk_const("selector.r3_stage","updater.exec_stage");
    mk_const("protocol.idle","protocol.2bit");
    mk_const("selector.r9_exec","updater.exec_var");
    mk_const("selector.r3_exec","updater.exec_var");
    mk_const("selector.r9_rate","updater.exec_var");
    mk_const("selector.r10_count_rate","updater.exec_var");
    mk_const("selector.r7_rate","updater.exec_var");
    mk_const("selector.r1_stage","updater.exec_stage");
    mk_const("selector.r8_count_rate","updater.exec_var");
    mk_const("selector.r11_stage","updater.exec_stage");
    mk_const("__ts0_a","Bool");
    add("(assert (and (= __ts0_a (bvuge protocol.r_codY #b0000000001)) "
"     (= protocol.idle #b0) "
"     __ts0_a))");
}
bool ext__protocol__update_r1_gen::generate(test_v2& obj) {
    push();
    slvr.add(__to_solver(*this,apply("protocol.r_codY"),obj.protocol__r_codY));
    slvr.add(__to_solver(*this,apply("protocol.idle"),obj.protocol__idle));
    alits.clear();

    // std::cout << slvr << std::endl;
    bool __res = solve();
    if (__res) {

    }
    pop();
    obj.___ivy_gen = this;
    return __res;
}
void ext__protocol__update_r1_gen::execute(test_v2& obj){
    __ivy_out << "> protocol.update_r1" << std::endl;
    obj.ext__protocol__update_r1();
}
class ext__protocol__update_r3_gen : public gen {
  public:
    ext__protocol__update_r3_gen();
    bool generate(test_v2&);
    void execute(test_v2&);
};
ext__protocol__update_r3_gen::ext__protocol__update_r3_gen(){
    mk_bv("protocol.2bit",1);
    mk_bv("updater.exec_stage",3);
    mk_bv("updater.num",10);
    mk_bv("updater.exec_var",8);
    mk_const("selector.r12_rate","updater.exec_var");
    mk_const("protocol.r_SigD_hag","updater.num");
    mk_const("protocol.r_CodY_flache","updater.num");
    mk_const("selector.r12_count_rate","updater.exec_var");
    mk_const("selector.r6_exec","updater.exec_var");
    mk_const("selector.r12_exec","updater.exec_var");
    mk_const("selector.r1_exec","updater.exec_var");
    mk_const("selector.r1_count_rate","updater.exec_var");
    mk_const("selector.r8_count","updater.exec_var");
    mk_const("selector.r5_count_rate","updater.exec_var");
    mk_const("selector.r6_rate","updater.exec_var");
    mk_const("selector.r1_count","updater.exec_var");
    mk_const("selector.r2_exec","updater.exec_var");
    mk_const("protocol.r_SigD","updater.num");
    mk_const("protocol.r_hag","updater.num");
    mk_const("selector.r5_exec","updater.exec_var");
    mk_const("selector.r2_count","updater.exec_var");
    mk_const("protocol.r_codY","updater.num");
    mk_const("selector.r10_exec","updater.exec_var");
    mk_const("selector.r4_stage","updater.exec_stage");
    mk_const("selector.r3_count","updater.exec_var");
    mk_const("selector.r7_count_rate","updater.exec_var");
    mk_const("selector.r2_count_rate","updater.exec_var");
    mk_const("selector.r8_exec","updater.exec_var");
    mk_const("protocol.r_CodY_hag","updater.num");
    mk_const("selector.r7_stage","updater.exec_stage");
    mk_const("selector.r12_count","updater.exec_var");
    mk_const("protocol.r_CodY","updater.num");
    mk_const("selector.r10_stage","updater.exec_stage");
    mk_const("selector.r9_count","updater.exec_var");
    mk_const("selector.r4_count","updater.exec_var");
    mk_const("selector.r7_count","updater.exec_var");
    mk_const("selector.r11_count_rate","updater.exec_var");
    mk_const("selector.r2_rate","updater.exec_var");
    mk_const("selector.r1_rate","updater.exec_var");
    mk_const("selector.r4_exec","updater.exec_var");
    mk_const("selector.r12_stage","updater.exec_stage");
    mk_const("selector.r11_rate","updater.exec_var");
    mk_const("selector.r2_stage","updater.exec_stage");
    mk_const("protocol.r_flache","updater.num");
    mk_const("selector.r10_rate","updater.exec_var");
    mk_const("selector.r5_stage","updater.exec_stage");
    mk_const("selector.r11_exec","updater.exec_var");
    mk_const("selector.r8_stage","updater.exec_stage");
    mk_const("selector.r4_count_rate","updater.exec_var");
    mk_const("selector.r3_count_rate","updater.exec_var");
    mk_const("_generating","Bool");
    mk_const("selector.r4_rate","updater.exec_var");
    mk_const("selector.r11_count","updater.exec_var");
    mk_const("selector.r10_count","updater.exec_var");
    mk_const("selector.r9_count_rate","updater.exec_var");
    mk_const("selector.r6_stage","updater.exec_stage");
    mk_const("selector.r7_exec","updater.exec_var");
    mk_const("selector.r9_stage","updater.exec_stage");
    mk_const("selector.r5_count","updater.exec_var");
    mk_const("selector.r8_rate","updater.exec_var");
    mk_const("selector.r6_count","updater.exec_var");
    mk_const("protocol.r_Hag","updater.num");
    mk_const("selector.r5_rate","updater.exec_var");
    mk_const("selector.r6_count_rate","updater.exec_var");
    mk_const("selector.r3_rate","updater.exec_var");
    mk_const("selector.r3_stage","updater.exec_stage");
    mk_const("protocol.idle","protocol.2bit");
    mk_const("selector.r9_exec","updater.exec_var");
    mk_const("selector.r3_exec","updater.exec_var");
    mk_const("selector.r9_rate","updater.exec_var");
    mk_const("selector.r10_count_rate","updater.exec_var");
    mk_const("selector.r7_rate","updater.exec_var");
    mk_const("selector.r1_stage","updater.exec_stage");
    mk_const("selector.r8_count_rate","updater.exec_var");
    mk_const("selector.r11_stage","updater.exec_stage");
    mk_const("__ts0_a","Bool");
    add("(assert (and (= __ts0_a (bvuge protocol.r_flache #b0000000001)) "
"     (= protocol.idle #b0) "
"     __ts0_a))");
}
bool ext__protocol__update_r3_gen::generate(test_v2& obj) {
    push();
    slvr.add(__to_solver(*this,apply("protocol.r_flache"),obj.protocol__r_flache));
    slvr.add(__to_solver(*this,apply("protocol.idle"),obj.protocol__idle));
    alits.clear();

    // std::cout << slvr << std::endl;
    bool __res = solve();
    if (__res) {

    }
    pop();
    obj.___ivy_gen = this;
    return __res;
}
void ext__protocol__update_r3_gen::execute(test_v2& obj){
    __ivy_out << "> protocol.update_r3" << std::endl;
    obj.ext__protocol__update_r3();
}
class ext__protocol__update_r2_gen : public gen {
  public:
    ext__protocol__update_r2_gen();
    bool generate(test_v2&);
    void execute(test_v2&);
};
ext__protocol__update_r2_gen::ext__protocol__update_r2_gen(){
    mk_bv("protocol.2bit",1);
    mk_bv("updater.exec_stage",3);
    mk_bv("updater.num",10);
    mk_bv("updater.exec_var",8);
    mk_const("selector.r12_rate","updater.exec_var");
    mk_const("protocol.r_SigD_hag","updater.num");
    mk_const("protocol.r_CodY_flache","updater.num");
    mk_const("selector.r12_count_rate","updater.exec_var");
    mk_const("selector.r6_exec","updater.exec_var");
    mk_const("selector.r12_exec","updater.exec_var");
    mk_const("selector.r1_exec","updater.exec_var");
    mk_const("selector.r1_count_rate","updater.exec_var");
    mk_const("selector.r8_count","updater.exec_var");
    mk_const("selector.r5_count_rate","updater.exec_var");
    mk_const("selector.r6_rate","updater.exec_var");
    mk_const("selector.r1_count","updater.exec_var");
    mk_const("selector.r2_exec","updater.exec_var");
    mk_const("protocol.r_SigD","updater.num");
    mk_const("protocol.r_hag","updater.num");
    mk_const("selector.r5_exec","updater.exec_var");
    mk_const("selector.r2_count","updater.exec_var");
    mk_const("protocol.r_codY","updater.num");
    mk_const("selector.r10_exec","updater.exec_var");
    mk_const("selector.r4_stage","updater.exec_stage");
    mk_const("selector.r3_count","updater.exec_var");
    mk_const("selector.r7_count_rate","updater.exec_var");
    mk_const("selector.r2_count_rate","updater.exec_var");
    mk_const("selector.r8_exec","updater.exec_var");
    mk_const("protocol.r_CodY_hag","updater.num");
    mk_const("selector.r7_stage","updater.exec_stage");
    mk_const("selector.r12_count","updater.exec_var");
    mk_const("protocol.r_CodY","updater.num");
    mk_const("selector.r10_stage","updater.exec_stage");
    mk_const("selector.r9_count","updater.exec_var");
    mk_const("selector.r4_count","updater.exec_var");
    mk_const("selector.r7_count","updater.exec_var");
    mk_const("selector.r11_count_rate","updater.exec_var");
    mk_const("selector.r2_rate","updater.exec_var");
    mk_const("selector.r1_rate","updater.exec_var");
    mk_const("selector.r4_exec","updater.exec_var");
    mk_const("selector.r12_stage","updater.exec_stage");
    mk_const("selector.r11_rate","updater.exec_var");
    mk_const("selector.r2_stage","updater.exec_stage");
    mk_const("protocol.r_flache","updater.num");
    mk_const("selector.r10_rate","updater.exec_var");
    mk_const("selector.r5_stage","updater.exec_stage");
    mk_const("selector.r11_exec","updater.exec_var");
    mk_const("selector.r8_stage","updater.exec_stage");
    mk_const("selector.r4_count_rate","updater.exec_var");
    mk_const("selector.r3_count_rate","updater.exec_var");
    mk_const("_generating","Bool");
    mk_const("selector.r4_rate","updater.exec_var");
    mk_const("selector.r11_count","updater.exec_var");
    mk_const("selector.r10_count","updater.exec_var");
    mk_const("selector.r9_count_rate","updater.exec_var");
    mk_const("selector.r6_stage","updater.exec_stage");
    mk_const("selector.r7_exec","updater.exec_var");
    mk_const("selector.r9_stage","updater.exec_stage");
    mk_const("selector.r5_count","updater.exec_var");
    mk_const("selector.r8_rate","updater.exec_var");
    mk_const("selector.r6_count","updater.exec_var");
    mk_const("protocol.r_Hag","updater.num");
    mk_const("selector.r5_rate","updater.exec_var");
    mk_const("selector.r6_count_rate","updater.exec_var");
    mk_const("selector.r3_rate","updater.exec_var");
    mk_const("selector.r3_stage","updater.exec_stage");
    mk_const("protocol.idle","protocol.2bit");
    mk_const("selector.r9_exec","updater.exec_var");
    mk_const("selector.r3_exec","updater.exec_var");
    mk_const("selector.r9_rate","updater.exec_var");
    mk_const("selector.r10_count_rate","updater.exec_var");
    mk_const("selector.r7_rate","updater.exec_var");
    mk_const("selector.r1_stage","updater.exec_stage");
    mk_const("selector.r8_count_rate","updater.exec_var");
    mk_const("selector.r11_stage","updater.exec_stage");
    mk_const("__ts0_a","Bool");
    add("(assert (and (= __ts0_a (bvuge protocol.r_CodY #b0000000001)) "
"     (= protocol.idle #b0) "
"     __ts0_a))");
}
bool ext__protocol__update_r2_gen::generate(test_v2& obj) {
    push();
    slvr.add(__to_solver(*this,apply("protocol.r_CodY"),obj.protocol__r_CodY));
    slvr.add(__to_solver(*this,apply("protocol.idle"),obj.protocol__idle));
    alits.clear();

    // std::cout << slvr << std::endl;
    bool __res = solve();
    if (__res) {

    }
    pop();
    obj.___ivy_gen = this;
    return __res;
}
void ext__protocol__update_r2_gen::execute(test_v2& obj){
    __ivy_out << "> protocol.update_r2" << std::endl;
    obj.ext__protocol__update_r2();
}
class ext__protocol__update_r5_gen : public gen {
  public:
    ext__protocol__update_r5_gen();
    bool generate(test_v2&);
    void execute(test_v2&);
};
ext__protocol__update_r5_gen::ext__protocol__update_r5_gen(){
    mk_bv("protocol.2bit",1);
    mk_bv("updater.exec_stage",3);
    mk_bv("updater.num",10);
    mk_bv("updater.exec_var",8);
    mk_const("selector.r12_rate","updater.exec_var");
    mk_const("protocol.r_SigD_hag","updater.num");
    mk_const("protocol.r_CodY_flache","updater.num");
    mk_const("selector.r12_count_rate","updater.exec_var");
    mk_const("selector.r6_exec","updater.exec_var");
    mk_const("selector.r12_exec","updater.exec_var");
    mk_const("selector.r1_exec","updater.exec_var");
    mk_const("selector.r1_count_rate","updater.exec_var");
    mk_const("selector.r8_count","updater.exec_var");
    mk_const("selector.r5_count_rate","updater.exec_var");
    mk_const("selector.r6_rate","updater.exec_var");
    mk_const("selector.r1_count","updater.exec_var");
    mk_const("selector.r2_exec","updater.exec_var");
    mk_const("protocol.r_SigD","updater.num");
    mk_const("protocol.r_hag","updater.num");
    mk_const("selector.r5_exec","updater.exec_var");
    mk_const("selector.r2_count","updater.exec_var");
    mk_const("protocol.r_codY","updater.num");
    mk_const("selector.r10_exec","updater.exec_var");
    mk_const("selector.r4_stage","updater.exec_stage");
    mk_const("selector.r3_count","updater.exec_var");
    mk_const("selector.r7_count_rate","updater.exec_var");
    mk_const("selector.r2_count_rate","updater.exec_var");
    mk_const("selector.r8_exec","updater.exec_var");
    mk_const("protocol.r_CodY_hag","updater.num");
    mk_const("selector.r7_stage","updater.exec_stage");
    mk_const("selector.r12_count","updater.exec_var");
    mk_const("protocol.r_CodY","updater.num");
    mk_const("selector.r10_stage","updater.exec_stage");
    mk_const("selector.r9_count","updater.exec_var");
    mk_const("selector.r4_count","updater.exec_var");
    mk_const("selector.r7_count","updater.exec_var");
    mk_const("selector.r11_count_rate","updater.exec_var");
    mk_const("selector.r2_rate","updater.exec_var");
    mk_const("selector.r1_rate","updater.exec_var");
    mk_const("selector.r4_exec","updater.exec_var");
    mk_const("selector.r12_stage","updater.exec_stage");
    mk_const("selector.r11_rate","updater.exec_var");
    mk_const("selector.r2_stage","updater.exec_stage");
    mk_const("protocol.r_flache","updater.num");
    mk_const("selector.r10_rate","updater.exec_var");
    mk_const("selector.r5_stage","updater.exec_stage");
    mk_const("selector.r11_exec","updater.exec_var");
    mk_const("selector.r8_stage","updater.exec_stage");
    mk_const("selector.r4_count_rate","updater.exec_var");
    mk_const("selector.r3_count_rate","updater.exec_var");
    mk_const("_generating","Bool");
    mk_const("selector.r4_rate","updater.exec_var");
    mk_const("selector.r11_count","updater.exec_var");
    mk_const("selector.r10_count","updater.exec_var");
    mk_const("selector.r9_count_rate","updater.exec_var");
    mk_const("selector.r6_stage","updater.exec_stage");
    mk_const("selector.r7_exec","updater.exec_var");
    mk_const("selector.r9_stage","updater.exec_stage");
    mk_const("selector.r5_count","updater.exec_var");
    mk_const("selector.r8_rate","updater.exec_var");
    mk_const("selector.r6_count","updater.exec_var");
    mk_const("protocol.r_Hag","updater.num");
    mk_const("selector.r5_rate","updater.exec_var");
    mk_const("selector.r6_count_rate","updater.exec_var");
    mk_const("selector.r3_rate","updater.exec_var");
    mk_const("selector.r3_stage","updater.exec_stage");
    mk_const("protocol.idle","protocol.2bit");
    mk_const("selector.r9_exec","updater.exec_var");
    mk_const("selector.r3_exec","updater.exec_var");
    mk_const("selector.r9_rate","updater.exec_var");
    mk_const("selector.r10_count_rate","updater.exec_var");
    mk_const("selector.r7_rate","updater.exec_var");
    mk_const("selector.r1_stage","updater.exec_stage");
    mk_const("selector.r8_count_rate","updater.exec_var");
    mk_const("selector.r11_stage","updater.exec_stage");
    mk_const("__ts0_a","Bool");
    add("(assert (and (= __ts0_a (bvuge protocol.r_SigD_hag #b0000000001)) "
"     (= protocol.idle #b0) "
"     __ts0_a))");
}
bool ext__protocol__update_r5_gen::generate(test_v2& obj) {
    push();
    slvr.add(__to_solver(*this,apply("protocol.r_SigD_hag"),obj.protocol__r_SigD_hag));
    slvr.add(__to_solver(*this,apply("protocol.idle"),obj.protocol__idle));
    alits.clear();

    // std::cout << slvr << std::endl;
    bool __res = solve();
    if (__res) {

    }
    pop();
    obj.___ivy_gen = this;
    return __res;
}
void ext__protocol__update_r5_gen::execute(test_v2& obj){
    __ivy_out << "> protocol.update_r5" << std::endl;
    obj.ext__protocol__update_r5();
}
class ext__protocol__update_r4_gen : public gen {
  public:
    ext__protocol__update_r4_gen();
    bool generate(test_v2&);
    void execute(test_v2&);
};
ext__protocol__update_r4_gen::ext__protocol__update_r4_gen(){
    mk_bv("protocol.2bit",1);
    mk_bv("updater.exec_stage",3);
    mk_bv("updater.num",10);
    mk_bv("updater.exec_var",8);
    mk_const("selector.r12_rate","updater.exec_var");
    mk_const("protocol.r_SigD_hag","updater.num");
    mk_const("protocol.r_CodY_flache","updater.num");
    mk_const("selector.r12_count_rate","updater.exec_var");
    mk_const("selector.r6_exec","updater.exec_var");
    mk_const("selector.r12_exec","updater.exec_var");
    mk_const("selector.r1_exec","updater.exec_var");
    mk_const("selector.r1_count_rate","updater.exec_var");
    mk_const("selector.r8_count","updater.exec_var");
    mk_const("selector.r5_count_rate","updater.exec_var");
    mk_const("selector.r6_rate","updater.exec_var");
    mk_const("selector.r1_count","updater.exec_var");
    mk_const("selector.r2_exec","updater.exec_var");
    mk_const("protocol.r_SigD","updater.num");
    mk_const("protocol.r_hag","updater.num");
    mk_const("selector.r5_exec","updater.exec_var");
    mk_const("selector.r2_count","updater.exec_var");
    mk_const("protocol.r_codY","updater.num");
    mk_const("selector.r10_exec","updater.exec_var");
    mk_const("selector.r4_stage","updater.exec_stage");
    mk_const("selector.r3_count","updater.exec_var");
    mk_const("selector.r7_count_rate","updater.exec_var");
    mk_const("selector.r2_count_rate","updater.exec_var");
    mk_const("selector.r8_exec","updater.exec_var");
    mk_const("protocol.r_CodY_hag","updater.num");
    mk_const("selector.r7_stage","updater.exec_stage");
    mk_const("selector.r12_count","updater.exec_var");
    mk_const("protocol.r_CodY","updater.num");
    mk_const("selector.r10_stage","updater.exec_stage");
    mk_const("selector.r9_count","updater.exec_var");
    mk_const("selector.r4_count","updater.exec_var");
    mk_const("selector.r7_count","updater.exec_var");
    mk_const("selector.r11_count_rate","updater.exec_var");
    mk_const("selector.r2_rate","updater.exec_var");
    mk_const("selector.r1_rate","updater.exec_var");
    mk_const("selector.r4_exec","updater.exec_var");
    mk_const("selector.r12_stage","updater.exec_stage");
    mk_const("selector.r11_rate","updater.exec_var");
    mk_const("selector.r2_stage","updater.exec_stage");
    mk_const("protocol.r_flache","updater.num");
    mk_const("selector.r10_rate","updater.exec_var");
    mk_const("selector.r5_stage","updater.exec_stage");
    mk_const("selector.r11_exec","updater.exec_var");
    mk_const("selector.r8_stage","updater.exec_stage");
    mk_const("selector.r4_count_rate","updater.exec_var");
    mk_const("selector.r3_count_rate","updater.exec_var");
    mk_const("_generating","Bool");
    mk_const("selector.r4_rate","updater.exec_var");
    mk_const("selector.r11_count","updater.exec_var");
    mk_const("selector.r10_count","updater.exec_var");
    mk_const("selector.r9_count_rate","updater.exec_var");
    mk_const("selector.r6_stage","updater.exec_stage");
    mk_const("selector.r7_exec","updater.exec_var");
    mk_const("selector.r9_stage","updater.exec_stage");
    mk_const("selector.r5_count","updater.exec_var");
    mk_const("selector.r8_rate","updater.exec_var");
    mk_const("selector.r6_count","updater.exec_var");
    mk_const("protocol.r_Hag","updater.num");
    mk_const("selector.r5_rate","updater.exec_var");
    mk_const("selector.r6_count_rate","updater.exec_var");
    mk_const("selector.r3_rate","updater.exec_var");
    mk_const("selector.r3_stage","updater.exec_stage");
    mk_const("protocol.idle","protocol.2bit");
    mk_const("selector.r9_exec","updater.exec_var");
    mk_const("selector.r3_exec","updater.exec_var");
    mk_const("selector.r9_rate","updater.exec_var");
    mk_const("selector.r10_count_rate","updater.exec_var");
    mk_const("selector.r7_rate","updater.exec_var");
    mk_const("selector.r1_stage","updater.exec_stage");
    mk_const("selector.r8_count_rate","updater.exec_var");
    mk_const("selector.r11_stage","updater.exec_stage");
    mk_const("__ts0_a","Bool");
    add("(assert (and (= __ts0_a (bvuge protocol.r_SigD #b0000000001)) "
"     (= protocol.idle #b0) "
"     __ts0_a))");
}
bool ext__protocol__update_r4_gen::generate(test_v2& obj) {
    push();
    slvr.add(__to_solver(*this,apply("protocol.r_SigD"),obj.protocol__r_SigD));
    slvr.add(__to_solver(*this,apply("protocol.idle"),obj.protocol__idle));
    alits.clear();

    // std::cout << slvr << std::endl;
    bool __res = solve();
    if (__res) {

    }
    pop();
    obj.___ivy_gen = this;
    return __res;
}
void ext__protocol__update_r4_gen::execute(test_v2& obj){
    __ivy_out << "> protocol.update_r4" << std::endl;
    obj.ext__protocol__update_r4();
}
class ext__protocol__update_r11_gen : public gen {
  public:
    ext__protocol__update_r11_gen();
    bool generate(test_v2&);
    void execute(test_v2&);
};
ext__protocol__update_r11_gen::ext__protocol__update_r11_gen(){
    mk_bv("protocol.2bit",1);
    mk_bv("updater.exec_stage",3);
    mk_bv("updater.num",10);
    mk_bv("updater.exec_var",8);
    mk_const("selector.r12_rate","updater.exec_var");
    mk_const("protocol.r_SigD_hag","updater.num");
    mk_const("protocol.r_CodY_flache","updater.num");
    mk_const("selector.r12_count_rate","updater.exec_var");
    mk_const("selector.r6_exec","updater.exec_var");
    mk_const("selector.r12_exec","updater.exec_var");
    mk_const("selector.r1_exec","updater.exec_var");
    mk_const("selector.r1_count_rate","updater.exec_var");
    mk_const("selector.r8_count","updater.exec_var");
    mk_const("selector.r5_count_rate","updater.exec_var");
    mk_const("selector.r6_rate","updater.exec_var");
    mk_const("selector.r1_count","updater.exec_var");
    mk_const("selector.r2_exec","updater.exec_var");
    mk_const("protocol.r_SigD","updater.num");
    mk_const("protocol.r_hag","updater.num");
    mk_const("selector.r5_exec","updater.exec_var");
    mk_const("selector.r2_count","updater.exec_var");
    mk_const("protocol.r_codY","updater.num");
    mk_const("selector.r10_exec","updater.exec_var");
    mk_const("selector.r4_stage","updater.exec_stage");
    mk_const("selector.r3_count","updater.exec_var");
    mk_const("selector.r7_count_rate","updater.exec_var");
    mk_const("selector.r2_count_rate","updater.exec_var");
    mk_const("selector.r8_exec","updater.exec_var");
    mk_const("protocol.r_CodY_hag","updater.num");
    mk_const("selector.r7_stage","updater.exec_stage");
    mk_const("selector.r12_count","updater.exec_var");
    mk_const("protocol.r_CodY","updater.num");
    mk_const("selector.r10_stage","updater.exec_stage");
    mk_const("selector.r9_count","updater.exec_var");
    mk_const("selector.r4_count","updater.exec_var");
    mk_const("selector.r7_count","updater.exec_var");
    mk_const("selector.r11_count_rate","updater.exec_var");
    mk_const("selector.r2_rate","updater.exec_var");
    mk_const("selector.r1_rate","updater.exec_var");
    mk_const("selector.r4_exec","updater.exec_var");
    mk_const("selector.r12_stage","updater.exec_stage");
    mk_const("selector.r11_rate","updater.exec_var");
    mk_const("selector.r2_stage","updater.exec_stage");
    mk_const("protocol.r_flache","updater.num");
    mk_const("selector.r10_rate","updater.exec_var");
    mk_const("selector.r5_stage","updater.exec_stage");
    mk_const("selector.r11_exec","updater.exec_var");
    mk_const("selector.r8_stage","updater.exec_stage");
    mk_const("selector.r4_count_rate","updater.exec_var");
    mk_const("selector.r3_count_rate","updater.exec_var");
    mk_const("_generating","Bool");
    mk_const("selector.r4_rate","updater.exec_var");
    mk_const("selector.r11_count","updater.exec_var");
    mk_const("selector.r10_count","updater.exec_var");
    mk_const("selector.r9_count_rate","updater.exec_var");
    mk_const("selector.r6_stage","updater.exec_stage");
    mk_const("selector.r7_exec","updater.exec_var");
    mk_const("selector.r9_stage","updater.exec_stage");
    mk_const("selector.r5_count","updater.exec_var");
    mk_const("selector.r8_rate","updater.exec_var");
    mk_const("selector.r6_count","updater.exec_var");
    mk_const("protocol.r_Hag","updater.num");
    mk_const("selector.r5_rate","updater.exec_var");
    mk_const("selector.r6_count_rate","updater.exec_var");
    mk_const("selector.r3_rate","updater.exec_var");
    mk_const("selector.r3_stage","updater.exec_stage");
    mk_const("protocol.idle","protocol.2bit");
    mk_const("selector.r9_exec","updater.exec_var");
    mk_const("selector.r3_exec","updater.exec_var");
    mk_const("selector.r9_rate","updater.exec_var");
    mk_const("selector.r10_count_rate","updater.exec_var");
    mk_const("selector.r7_rate","updater.exec_var");
    mk_const("selector.r1_stage","updater.exec_stage");
    mk_const("selector.r8_count_rate","updater.exec_var");
    mk_const("selector.r11_stage","updater.exec_stage");
    mk_const("__ts0_a","Bool");
    add("(assert (and (= __ts0_a "
"        (and (bvuge protocol.r_CodY #b0000000001) "
"             (bvuge protocol.r_hag #b0000000001))) "
"     (= protocol.idle #b0) "
"     __ts0_a))");
}
bool ext__protocol__update_r11_gen::generate(test_v2& obj) {
    push();
    slvr.add(__to_solver(*this,apply("protocol.r_hag"),obj.protocol__r_hag));
    slvr.add(__to_solver(*this,apply("protocol.r_CodY"),obj.protocol__r_CodY));
    slvr.add(__to_solver(*this,apply("protocol.idle"),obj.protocol__idle));
    alits.clear();

    // std::cout << slvr << std::endl;
    bool __res = solve();
    if (__res) {

    }
    pop();
    obj.___ivy_gen = this;
    return __res;
}
void ext__protocol__update_r11_gen::execute(test_v2& obj){
    __ivy_out << "> protocol.update_r11" << std::endl;
    obj.ext__protocol__update_r11();
}
class ext__protocol__update_r10_gen : public gen {
  public:
    ext__protocol__update_r10_gen();
    bool generate(test_v2&);
    void execute(test_v2&);
};
ext__protocol__update_r10_gen::ext__protocol__update_r10_gen(){
    mk_bv("protocol.2bit",1);
    mk_bv("updater.exec_stage",3);
    mk_bv("updater.num",10);
    mk_bv("updater.exec_var",8);
    mk_const("selector.r12_rate","updater.exec_var");
    mk_const("protocol.r_SigD_hag","updater.num");
    mk_const("protocol.r_CodY_flache","updater.num");
    mk_const("selector.r12_count_rate","updater.exec_var");
    mk_const("selector.r6_exec","updater.exec_var");
    mk_const("selector.r12_exec","updater.exec_var");
    mk_const("selector.r1_exec","updater.exec_var");
    mk_const("selector.r1_count_rate","updater.exec_var");
    mk_const("selector.r8_count","updater.exec_var");
    mk_const("selector.r5_count_rate","updater.exec_var");
    mk_const("selector.r6_rate","updater.exec_var");
    mk_const("selector.r1_count","updater.exec_var");
    mk_const("selector.r2_exec","updater.exec_var");
    mk_const("protocol.r_SigD","updater.num");
    mk_const("protocol.r_hag","updater.num");
    mk_const("selector.r5_exec","updater.exec_var");
    mk_const("selector.r2_count","updater.exec_var");
    mk_const("protocol.r_codY","updater.num");
    mk_const("selector.r10_exec","updater.exec_var");
    mk_const("selector.r4_stage","updater.exec_stage");
    mk_const("selector.r3_count","updater.exec_var");
    mk_const("selector.r7_count_rate","updater.exec_var");
    mk_const("selector.r2_count_rate","updater.exec_var");
    mk_const("selector.r8_exec","updater.exec_var");
    mk_const("protocol.r_CodY_hag","updater.num");
    mk_const("selector.r7_stage","updater.exec_stage");
    mk_const("selector.r12_count","updater.exec_var");
    mk_const("protocol.r_CodY","updater.num");
    mk_const("selector.r10_stage","updater.exec_stage");
    mk_const("selector.r9_count","updater.exec_var");
    mk_const("selector.r4_count","updater.exec_var");
    mk_const("selector.r7_count","updater.exec_var");
    mk_const("selector.r11_count_rate","updater.exec_var");
    mk_const("selector.r2_rate","updater.exec_var");
    mk_const("selector.r1_rate","updater.exec_var");
    mk_const("selector.r4_exec","updater.exec_var");
    mk_const("selector.r12_stage","updater.exec_stage");
    mk_const("selector.r11_rate","updater.exec_var");
    mk_const("selector.r2_stage","updater.exec_stage");
    mk_const("protocol.r_flache","updater.num");
    mk_const("selector.r10_rate","updater.exec_var");
    mk_const("selector.r5_stage","updater.exec_stage");
    mk_const("selector.r11_exec","updater.exec_var");
    mk_const("selector.r8_stage","updater.exec_stage");
    mk_const("selector.r4_count_rate","updater.exec_var");
    mk_const("selector.r3_count_rate","updater.exec_var");
    mk_const("_generating","Bool");
    mk_const("selector.r4_rate","updater.exec_var");
    mk_const("selector.r11_count","updater.exec_var");
    mk_const("selector.r10_count","updater.exec_var");
    mk_const("selector.r9_count_rate","updater.exec_var");
    mk_const("selector.r6_stage","updater.exec_stage");
    mk_const("selector.r7_exec","updater.exec_var");
    mk_const("selector.r9_stage","updater.exec_stage");
    mk_const("selector.r5_count","updater.exec_var");
    mk_const("selector.r8_rate","updater.exec_var");
    mk_const("selector.r6_count","updater.exec_var");
    mk_const("protocol.r_Hag","updater.num");
    mk_const("selector.r5_rate","updater.exec_var");
    mk_const("selector.r6_count_rate","updater.exec_var");
    mk_const("selector.r3_rate","updater.exec_var");
    mk_const("selector.r3_stage","updater.exec_stage");
    mk_const("protocol.idle","protocol.2bit");
    mk_const("selector.r9_exec","updater.exec_var");
    mk_const("selector.r3_exec","updater.exec_var");
    mk_const("selector.r9_rate","updater.exec_var");
    mk_const("selector.r10_count_rate","updater.exec_var");
    mk_const("selector.r7_rate","updater.exec_var");
    mk_const("selector.r1_stage","updater.exec_stage");
    mk_const("selector.r8_count_rate","updater.exec_var");
    mk_const("selector.r11_stage","updater.exec_stage");
    mk_const("__ts0_a","Bool");
    add("(assert (and (= __ts0_a (bvuge protocol.r_CodY_flache #b0000000001)) "
"     (= protocol.idle #b0) "
"     __ts0_a))");
}
bool ext__protocol__update_r10_gen::generate(test_v2& obj) {
    push();
    slvr.add(__to_solver(*this,apply("protocol.r_CodY_flache"),obj.protocol__r_CodY_flache));
    slvr.add(__to_solver(*this,apply("protocol.idle"),obj.protocol__idle));
    alits.clear();

    // std::cout << slvr << std::endl;
    bool __res = solve();
    if (__res) {

    }
    pop();
    obj.___ivy_gen = this;
    return __res;
}
void ext__protocol__update_r10_gen::execute(test_v2& obj){
    __ivy_out << "> protocol.update_r10" << std::endl;
    obj.ext__protocol__update_r10();
}
class ext__protocol__update_r12_gen : public gen {
  public:
    ext__protocol__update_r12_gen();
    bool generate(test_v2&);
    void execute(test_v2&);
};
ext__protocol__update_r12_gen::ext__protocol__update_r12_gen(){
    mk_bv("protocol.2bit",1);
    mk_bv("updater.exec_stage",3);
    mk_bv("updater.num",10);
    mk_bv("updater.exec_var",8);
    mk_const("selector.r12_rate","updater.exec_var");
    mk_const("protocol.r_SigD_hag","updater.num");
    mk_const("protocol.r_CodY_flache","updater.num");
    mk_const("selector.r12_count_rate","updater.exec_var");
    mk_const("selector.r6_exec","updater.exec_var");
    mk_const("selector.r12_exec","updater.exec_var");
    mk_const("selector.r1_exec","updater.exec_var");
    mk_const("selector.r1_count_rate","updater.exec_var");
    mk_const("selector.r8_count","updater.exec_var");
    mk_const("selector.r5_count_rate","updater.exec_var");
    mk_const("selector.r6_rate","updater.exec_var");
    mk_const("selector.r1_count","updater.exec_var");
    mk_const("selector.r2_exec","updater.exec_var");
    mk_const("protocol.r_SigD","updater.num");
    mk_const("protocol.r_hag","updater.num");
    mk_const("selector.r5_exec","updater.exec_var");
    mk_const("selector.r2_count","updater.exec_var");
    mk_const("protocol.r_codY","updater.num");
    mk_const("selector.r10_exec","updater.exec_var");
    mk_const("selector.r4_stage","updater.exec_stage");
    mk_const("selector.r3_count","updater.exec_var");
    mk_const("selector.r7_count_rate","updater.exec_var");
    mk_const("selector.r2_count_rate","updater.exec_var");
    mk_const("selector.r8_exec","updater.exec_var");
    mk_const("protocol.r_CodY_hag","updater.num");
    mk_const("selector.r7_stage","updater.exec_stage");
    mk_const("selector.r12_count","updater.exec_var");
    mk_const("protocol.r_CodY","updater.num");
    mk_const("selector.r10_stage","updater.exec_stage");
    mk_const("selector.r9_count","updater.exec_var");
    mk_const("selector.r4_count","updater.exec_var");
    mk_const("selector.r7_count","updater.exec_var");
    mk_const("selector.r11_count_rate","updater.exec_var");
    mk_const("selector.r2_rate","updater.exec_var");
    mk_const("selector.r1_rate","updater.exec_var");
    mk_const("selector.r4_exec","updater.exec_var");
    mk_const("selector.r12_stage","updater.exec_stage");
    mk_const("selector.r11_rate","updater.exec_var");
    mk_const("selector.r2_stage","updater.exec_stage");
    mk_const("protocol.r_flache","updater.num");
    mk_const("selector.r10_rate","updater.exec_var");
    mk_const("selector.r5_stage","updater.exec_stage");
    mk_const("selector.r11_exec","updater.exec_var");
    mk_const("selector.r8_stage","updater.exec_stage");
    mk_const("selector.r4_count_rate","updater.exec_var");
    mk_const("selector.r3_count_rate","updater.exec_var");
    mk_const("_generating","Bool");
    mk_const("selector.r4_rate","updater.exec_var");
    mk_const("selector.r11_count","updater.exec_var");
    mk_const("selector.r10_count","updater.exec_var");
    mk_const("selector.r9_count_rate","updater.exec_var");
    mk_const("selector.r6_stage","updater.exec_stage");
    mk_const("selector.r7_exec","updater.exec_var");
    mk_const("selector.r9_stage","updater.exec_stage");
    mk_const("selector.r5_count","updater.exec_var");
    mk_const("selector.r8_rate","updater.exec_var");
    mk_const("selector.r6_count","updater.exec_var");
    mk_const("protocol.r_Hag","updater.num");
    mk_const("selector.r5_rate","updater.exec_var");
    mk_const("selector.r6_count_rate","updater.exec_var");
    mk_const("selector.r3_rate","updater.exec_var");
    mk_const("selector.r3_stage","updater.exec_stage");
    mk_const("protocol.idle","protocol.2bit");
    mk_const("selector.r9_exec","updater.exec_var");
    mk_const("selector.r3_exec","updater.exec_var");
    mk_const("selector.r9_rate","updater.exec_var");
    mk_const("selector.r10_count_rate","updater.exec_var");
    mk_const("selector.r7_rate","updater.exec_var");
    mk_const("selector.r1_stage","updater.exec_stage");
    mk_const("selector.r8_count_rate","updater.exec_var");
    mk_const("selector.r11_stage","updater.exec_stage");
    mk_const("__ts0_a","Bool");
    add("(assert (and (= __ts0_a (bvuge protocol.r_CodY_hag #b0000000001)) "
"     (= protocol.idle #b0) "
"     __ts0_a))");
}
bool ext__protocol__update_r12_gen::generate(test_v2& obj) {
    push();
    slvr.add(__to_solver(*this,apply("protocol.r_CodY_hag"),obj.protocol__r_CodY_hag));
    slvr.add(__to_solver(*this,apply("protocol.idle"),obj.protocol__idle));
    alits.clear();

    // std::cout << slvr << std::endl;
    bool __res = solve();
    if (__res) {

    }
    pop();
    obj.___ivy_gen = this;
    return __res;
}
void ext__protocol__update_r12_gen::execute(test_v2& obj){
    __ivy_out << "> protocol.update_r12" << std::endl;
    obj.ext__protocol__update_r12();
}
class ext__protocol__update_r7_gen : public gen {
  public:
    ext__protocol__update_r7_gen();
    bool generate(test_v2&);
    void execute(test_v2&);
};
ext__protocol__update_r7_gen::ext__protocol__update_r7_gen(){
    mk_bv("protocol.2bit",1);
    mk_bv("updater.exec_stage",3);
    mk_bv("updater.num",10);
    mk_bv("updater.exec_var",8);
    mk_const("selector.r12_rate","updater.exec_var");
    mk_const("protocol.r_SigD_hag","updater.num");
    mk_const("protocol.r_CodY_flache","updater.num");
    mk_const("selector.r12_count_rate","updater.exec_var");
    mk_const("selector.r6_exec","updater.exec_var");
    mk_const("selector.r12_exec","updater.exec_var");
    mk_const("selector.r1_exec","updater.exec_var");
    mk_const("selector.r1_count_rate","updater.exec_var");
    mk_const("selector.r8_count","updater.exec_var");
    mk_const("selector.r5_count_rate","updater.exec_var");
    mk_const("selector.r6_rate","updater.exec_var");
    mk_const("selector.r1_count","updater.exec_var");
    mk_const("selector.r2_exec","updater.exec_var");
    mk_const("protocol.r_SigD","updater.num");
    mk_const("protocol.r_hag","updater.num");
    mk_const("selector.r5_exec","updater.exec_var");
    mk_const("selector.r2_count","updater.exec_var");
    mk_const("protocol.r_codY","updater.num");
    mk_const("selector.r10_exec","updater.exec_var");
    mk_const("selector.r4_stage","updater.exec_stage");
    mk_const("selector.r3_count","updater.exec_var");
    mk_const("selector.r7_count_rate","updater.exec_var");
    mk_const("selector.r2_count_rate","updater.exec_var");
    mk_const("selector.r8_exec","updater.exec_var");
    mk_const("protocol.r_CodY_hag","updater.num");
    mk_const("selector.r7_stage","updater.exec_stage");
    mk_const("selector.r12_count","updater.exec_var");
    mk_const("protocol.r_CodY","updater.num");
    mk_const("selector.r10_stage","updater.exec_stage");
    mk_const("selector.r9_count","updater.exec_var");
    mk_const("selector.r4_count","updater.exec_var");
    mk_const("selector.r7_count","updater.exec_var");
    mk_const("selector.r11_count_rate","updater.exec_var");
    mk_const("selector.r2_rate","updater.exec_var");
    mk_const("selector.r1_rate","updater.exec_var");
    mk_const("selector.r4_exec","updater.exec_var");
    mk_const("selector.r12_stage","updater.exec_stage");
    mk_const("selector.r11_rate","updater.exec_var");
    mk_const("selector.r2_stage","updater.exec_stage");
    mk_const("protocol.r_flache","updater.num");
    mk_const("selector.r10_rate","updater.exec_var");
    mk_const("selector.r5_stage","updater.exec_stage");
    mk_const("selector.r11_exec","updater.exec_var");
    mk_const("selector.r8_stage","updater.exec_stage");
    mk_const("selector.r4_count_rate","updater.exec_var");
    mk_const("selector.r3_count_rate","updater.exec_var");
    mk_const("_generating","Bool");
    mk_const("selector.r4_rate","updater.exec_var");
    mk_const("selector.r11_count","updater.exec_var");
    mk_const("selector.r10_count","updater.exec_var");
    mk_const("selector.r9_count_rate","updater.exec_var");
    mk_const("selector.r6_stage","updater.exec_stage");
    mk_const("selector.r7_exec","updater.exec_var");
    mk_const("selector.r9_stage","updater.exec_stage");
    mk_const("selector.r5_count","updater.exec_var");
    mk_const("selector.r8_rate","updater.exec_var");
    mk_const("selector.r6_count","updater.exec_var");
    mk_const("protocol.r_Hag","updater.num");
    mk_const("selector.r5_rate","updater.exec_var");
    mk_const("selector.r6_count_rate","updater.exec_var");
    mk_const("selector.r3_rate","updater.exec_var");
    mk_const("selector.r3_stage","updater.exec_stage");
    mk_const("protocol.idle","protocol.2bit");
    mk_const("selector.r9_exec","updater.exec_var");
    mk_const("selector.r3_exec","updater.exec_var");
    mk_const("selector.r9_rate","updater.exec_var");
    mk_const("selector.r10_count_rate","updater.exec_var");
    mk_const("selector.r7_rate","updater.exec_var");
    mk_const("selector.r1_stage","updater.exec_stage");
    mk_const("selector.r8_count_rate","updater.exec_var");
    mk_const("selector.r11_stage","updater.exec_stage");
    mk_const("__ts0_a","Bool");
    add("(assert (and (= __ts0_a "
"        (and (bvuge protocol.r_SigD #b0000000001) "
"             (bvuge protocol.r_hag #b0000000001))) "
"     (= protocol.idle #b0) "
"     __ts0_a))");
}
bool ext__protocol__update_r7_gen::generate(test_v2& obj) {
    push();
    slvr.add(__to_solver(*this,apply("protocol.r_SigD"),obj.protocol__r_SigD));
    slvr.add(__to_solver(*this,apply("protocol.r_hag"),obj.protocol__r_hag));
    slvr.add(__to_solver(*this,apply("protocol.idle"),obj.protocol__idle));
    alits.clear();

    // std::cout << slvr << std::endl;
    bool __res = solve();
    if (__res) {

    }
    pop();
    obj.___ivy_gen = this;
    return __res;
}
void ext__protocol__update_r7_gen::execute(test_v2& obj){
    __ivy_out << "> protocol.update_r7" << std::endl;
    obj.ext__protocol__update_r7();
}
class ext__protocol__update_r6_gen : public gen {
  public:
    ext__protocol__update_r6_gen();
    bool generate(test_v2&);
    void execute(test_v2&);
};
ext__protocol__update_r6_gen::ext__protocol__update_r6_gen(){
    mk_bv("protocol.2bit",1);
    mk_bv("updater.exec_stage",3);
    mk_bv("updater.num",10);
    mk_bv("updater.exec_var",8);
    mk_const("selector.r12_rate","updater.exec_var");
    mk_const("protocol.r_SigD_hag","updater.num");
    mk_const("protocol.r_CodY_flache","updater.num");
    mk_const("selector.r12_count_rate","updater.exec_var");
    mk_const("selector.r6_exec","updater.exec_var");
    mk_const("selector.r12_exec","updater.exec_var");
    mk_const("selector.r1_exec","updater.exec_var");
    mk_const("selector.r1_count_rate","updater.exec_var");
    mk_const("selector.r8_count","updater.exec_var");
    mk_const("selector.r5_count_rate","updater.exec_var");
    mk_const("selector.r6_rate","updater.exec_var");
    mk_const("selector.r1_count","updater.exec_var");
    mk_const("selector.r2_exec","updater.exec_var");
    mk_const("protocol.r_SigD","updater.num");
    mk_const("protocol.r_hag","updater.num");
    mk_const("selector.r5_exec","updater.exec_var");
    mk_const("selector.r2_count","updater.exec_var");
    mk_const("protocol.r_codY","updater.num");
    mk_const("selector.r10_exec","updater.exec_var");
    mk_const("selector.r4_stage","updater.exec_stage");
    mk_const("selector.r3_count","updater.exec_var");
    mk_const("selector.r7_count_rate","updater.exec_var");
    mk_const("selector.r2_count_rate","updater.exec_var");
    mk_const("selector.r8_exec","updater.exec_var");
    mk_const("protocol.r_CodY_hag","updater.num");
    mk_const("selector.r7_stage","updater.exec_stage");
    mk_const("selector.r12_count","updater.exec_var");
    mk_const("protocol.r_CodY","updater.num");
    mk_const("selector.r10_stage","updater.exec_stage");
    mk_const("selector.r9_count","updater.exec_var");
    mk_const("selector.r4_count","updater.exec_var");
    mk_const("selector.r7_count","updater.exec_var");
    mk_const("selector.r11_count_rate","updater.exec_var");
    mk_const("selector.r2_rate","updater.exec_var");
    mk_const("selector.r1_rate","updater.exec_var");
    mk_const("selector.r4_exec","updater.exec_var");
    mk_const("selector.r12_stage","updater.exec_stage");
    mk_const("selector.r11_rate","updater.exec_var");
    mk_const("selector.r2_stage","updater.exec_stage");
    mk_const("protocol.r_flache","updater.num");
    mk_const("selector.r10_rate","updater.exec_var");
    mk_const("selector.r5_stage","updater.exec_stage");
    mk_const("selector.r11_exec","updater.exec_var");
    mk_const("selector.r8_stage","updater.exec_stage");
    mk_const("selector.r4_count_rate","updater.exec_var");
    mk_const("selector.r3_count_rate","updater.exec_var");
    mk_const("_generating","Bool");
    mk_const("selector.r4_rate","updater.exec_var");
    mk_const("selector.r11_count","updater.exec_var");
    mk_const("selector.r10_count","updater.exec_var");
    mk_const("selector.r9_count_rate","updater.exec_var");
    mk_const("selector.r6_stage","updater.exec_stage");
    mk_const("selector.r7_exec","updater.exec_var");
    mk_const("selector.r9_stage","updater.exec_stage");
    mk_const("selector.r5_count","updater.exec_var");
    mk_const("selector.r8_rate","updater.exec_var");
    mk_const("selector.r6_count","updater.exec_var");
    mk_const("protocol.r_Hag","updater.num");
    mk_const("selector.r5_rate","updater.exec_var");
    mk_const("selector.r6_count_rate","updater.exec_var");
    mk_const("selector.r3_rate","updater.exec_var");
    mk_const("selector.r3_stage","updater.exec_stage");
    mk_const("protocol.idle","protocol.2bit");
    mk_const("selector.r9_exec","updater.exec_var");
    mk_const("selector.r3_exec","updater.exec_var");
    mk_const("selector.r9_rate","updater.exec_var");
    mk_const("selector.r10_count_rate","updater.exec_var");
    mk_const("selector.r7_rate","updater.exec_var");
    mk_const("selector.r1_stage","updater.exec_stage");
    mk_const("selector.r8_count_rate","updater.exec_var");
    mk_const("selector.r11_stage","updater.exec_stage");
    mk_const("__ts0_a","Bool");
    add("(assert (and (= __ts0_a (bvuge protocol.r_Hag #b0000000001)) "
"     (= protocol.idle #b0) "
"     __ts0_a))");
}
bool ext__protocol__update_r6_gen::generate(test_v2& obj) {
    push();
    slvr.add(__to_solver(*this,apply("protocol.r_Hag"),obj.protocol__r_Hag));
    slvr.add(__to_solver(*this,apply("protocol.idle"),obj.protocol__idle));
    alits.clear();

    // std::cout << slvr << std::endl;
    bool __res = solve();
    if (__res) {

    }
    pop();
    obj.___ivy_gen = this;
    return __res;
}
void ext__protocol__update_r6_gen::execute(test_v2& obj){
    __ivy_out << "> protocol.update_r6" << std::endl;
    obj.ext__protocol__update_r6();
}


int ask_ret(long long bound) {
    int res;
    while(true) {
        __ivy_out << "? ";
        std::cin >> res;
        if (res >= 0 && res < bound) 
            return res;
        std::cerr << "value out of range" << std::endl;
    }
}



    class test_v2_repl : public test_v2 {

    public:

    virtual void ivy_assert(bool truth,const char *msg){
        if (!truth) {
            __ivy_out << "assertion_failed(\"" << msg << "\")" << std::endl;
            std::cerr << msg << ": error: assertion failed\n";
            
            __ivy_exit(1);
        }
    }
    virtual void ivy_assume(bool truth,const char *msg){
        if (!truth) {
            __ivy_out << "assumption_failed(\"" << msg << "\")" << std::endl;
            std::cerr << msg << ": error: assumption failed\n";
            
            __ivy_exit(1);
        }
    }
    test_v2_repl() : test_v2(){}
    virtual void imp__goal__achieved(unsigned v){}
    virtual void imp__inspector__check_guard_r12(unsigned reactant1){}
    virtual void imp__inspector__check_guard_r10(unsigned reactant1){}
    virtual void imp__inspector__check_guard_r11(unsigned reactant1, unsigned reactant2){}
    virtual void imp__inspector__check_guard_r4(unsigned reactant1){}
    virtual void imp__inspector__check_guard_r5(unsigned reactant1){}
    virtual void imp__inspector__check_guard_r6(unsigned reactant1){}
    virtual void imp__inspector__check_guard_r7(unsigned reactant1, unsigned reactant2){}
    virtual void imp__inspector__check_guard_r1(unsigned reactant1){}
    virtual void imp__inspector__check_guard_r2(unsigned reactant1){}
    virtual void imp__inspector__check_guard_r3(unsigned reactant1){}
    virtual void imp__inspector__check_guard_r8(unsigned reactant1){}
    virtual void imp__inspector__check_guard_r9(unsigned reactant1, unsigned reactant2){}

    };

// Override methods to implement low-level network service

bool is_white(int c) {
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r');
}

bool is_ident(int c) {
    return c == '_' || c == '.' || (c >= 'A' &&  c <= 'Z')
        || (c >= 'a' &&  c <= 'z')
        || (c >= '0' &&  c <= '9');
}

void skip_white(const std::string& str, int &pos){
    while (pos < str.size() && is_white(str[pos]))
        pos++;
}

struct syntax_error {
    int pos;
    syntax_error(int pos) : pos(pos) {}
};

void throw_syntax(int pos){
    throw syntax_error(pos);
}

std::string get_ident(const std::string& str, int &pos) {
    std::string res = "";
    while (pos < str.size() && is_ident(str[pos])) {
        res.push_back(str[pos]);
        pos++;
    }
    if (res.size() == 0)
        throw_syntax(pos);
    return res;
}

ivy_value parse_value(const std::string& cmd, int &pos) {
    ivy_value res;
    res.pos = pos;
    skip_white(cmd,pos);
    if (pos < cmd.size() && cmd[pos] == '[') {
        while (true) {
            pos++;
            skip_white(cmd,pos);
            if (pos < cmd.size() && cmd[pos] == ']')
                break;
            res.fields.push_back(parse_value(cmd,pos));
            skip_white(cmd,pos);
            if (pos < cmd.size() && cmd[pos] == ']')
                break;
            if (!(pos < cmd.size() && cmd[pos] == ','))
                throw_syntax(pos);
        }
        pos++;
    }
    else if (pos < cmd.size() && cmd[pos] == '{') {
        while (true) {
            ivy_value field;
            pos++;
            skip_white(cmd,pos);
            field.atom = get_ident(cmd,pos);
            skip_white(cmd,pos);
            if (!(pos < cmd.size() && cmd[pos] == ':'))
                 throw_syntax(pos);
            pos++;
            skip_white(cmd,pos);
            field.fields.push_back(parse_value(cmd,pos));
            res.fields.push_back(field);
            skip_white(cmd,pos);
            if (pos < cmd.size() && cmd[pos] == '}')
                break;
            if (!(pos < cmd.size() && cmd[pos] == ','))
                throw_syntax(pos);
        }
        pos++;
    }
    else if (pos < cmd.size() && cmd[pos] == '"') {
        pos++;
        res.atom = "";
        while (pos < cmd.size() && cmd[pos] != '"') {
            char c = cmd[pos++];
            if (c == '\\') {
                if (pos == cmd.size())
                    throw_syntax(pos);
                c = cmd[pos++];
                c = (c == 'n') ? 10 : (c == 'r') ? 13 : (c == 't') ? 9 : c;
            }
            res.atom.push_back(c);
        }
        if(pos == cmd.size())
            throw_syntax(pos);
        pos++;
    }
    else 
        res.atom = get_ident(cmd,pos);
    return res;
}

void parse_command(const std::string &cmd, std::string &action, std::vector<ivy_value> &args) {
    int pos = 0;
    skip_white(cmd,pos);
    action = get_ident(cmd,pos);
    skip_white(cmd,pos);
    if (pos < cmd.size() && cmd[pos] == '(') {
        pos++;
        skip_white(cmd,pos);
        args.push_back(parse_value(cmd,pos));
        while(true) {
            skip_white(cmd,pos);
            if (!(pos < cmd.size() && cmd[pos] == ','))
                break;
            pos++;
            args.push_back(parse_value(cmd,pos));
        }
        if (!(pos < cmd.size() && cmd[pos] == ')'))
            throw_syntax(pos);
        pos++;
    }
    skip_white(cmd,pos);
    if (pos != cmd.size())
        throw_syntax(pos);
}

struct bad_arity {
    std::string action;
    int num;
    bad_arity(std::string &_action, unsigned _num) : action(_action), num(_num) {}
};

void check_arity(std::vector<ivy_value> &args, unsigned num, std::string &action) {
    if (args.size() != num)
        throw bad_arity(action,num);
}



class stdin_reader: public reader {
    std::string buf;
    std::string eof_flag;

public:
    bool eof(){
      return eof_flag.size();
    }
    virtual int fdes(){
        return 0;
    }
    virtual void read() {
        char tmp[257];
        int chars = ::read(0,tmp,256);
        if (chars == 0) {  // EOF
            if (buf.size())
                process(buf);
            eof_flag = "eof";
        }
        tmp[chars] = 0;
        buf += std::string(tmp);
        size_t pos;
        while ((pos = buf.find('\n')) != std::string::npos) {
            std::string line = buf.substr(0,pos+1);
            buf.erase(0,pos+1);
            process(line);
        }
    }
    virtual void process(const std::string &line) {
        __ivy_out << line;
    }
};

class cmd_reader: public stdin_reader {
    int lineno;
public:
    test_v2_repl &ivy;    

    cmd_reader(test_v2_repl &_ivy) : ivy(_ivy) {
        lineno = 1;
        if (isatty(fdes()))
            __ivy_out << "> "; __ivy_out.flush();
    }

    virtual void process(const std::string &cmd) {
        std::string action;
        std::vector<ivy_value> args;
        try {
            parse_command(cmd,action,args);
            ivy.__lock();

                if (action == "protocol.fail_test") {
                    check_arity(args,0,action);
                    ivy.ext__protocol__fail_test();
                }
                else
    
                if (action == "protocol.idling") {
                    check_arity(args,0,action);
                    ivy.ext__protocol__idling();
                }
                else
    
                if (action == "protocol.update_r1") {
                    check_arity(args,0,action);
                    ivy.ext__protocol__update_r1();
                }
                else
    
                if (action == "protocol.update_r10") {
                    check_arity(args,0,action);
                    ivy.ext__protocol__update_r10();
                }
                else
    
                if (action == "protocol.update_r11") {
                    check_arity(args,0,action);
                    ivy.ext__protocol__update_r11();
                }
                else
    
                if (action == "protocol.update_r12") {
                    check_arity(args,0,action);
                    ivy.ext__protocol__update_r12();
                }
                else
    
                if (action == "protocol.update_r2") {
                    check_arity(args,0,action);
                    ivy.ext__protocol__update_r2();
                }
                else
    
                if (action == "protocol.update_r3") {
                    check_arity(args,0,action);
                    ivy.ext__protocol__update_r3();
                }
                else
    
                if (action == "protocol.update_r4") {
                    check_arity(args,0,action);
                    ivy.ext__protocol__update_r4();
                }
                else
    
                if (action == "protocol.update_r5") {
                    check_arity(args,0,action);
                    ivy.ext__protocol__update_r5();
                }
                else
    
                if (action == "protocol.update_r6") {
                    check_arity(args,0,action);
                    ivy.ext__protocol__update_r6();
                }
                else
    
                if (action == "protocol.update_r7") {
                    check_arity(args,0,action);
                    ivy.ext__protocol__update_r7();
                }
                else
    
                if (action == "protocol.update_r8") {
                    check_arity(args,0,action);
                    ivy.ext__protocol__update_r8();
                }
                else
    
                if (action == "protocol.update_r9") {
                    check_arity(args,0,action);
                    ivy.ext__protocol__update_r9();
                }
                else
    
            {
                std::cerr << "undefined action: " << action << std::endl;
            }
            ivy.__unlock();
        }
        catch (syntax_error& err) {
            ivy.__unlock();
            std::cerr << "line " << lineno << ":" << err.pos << ": syntax error" << std::endl;
        }
        catch (out_of_bounds &err) {
            ivy.__unlock();
            std::cerr << "line " << lineno << ":" << err.pos << ": " << err.txt << " bad value" << std::endl;
        }
        catch (bad_arity &err) {
            ivy.__unlock();
            std::cerr << "action " << err.action << " takes " << err.num  << " input parameters" << std::endl;
        }
        if (isatty(fdes()))
            __ivy_out << "> "; __ivy_out.flush();
        lineno++;
    }
};



int main(int argc, char **argv){
        int test_iters = 100;
        int runs = 1;

    int seed = 1;
    int sleep_ms = 10;
    int final_ms = 0; 
    
    std::vector<char *> pargs; // positional args
    pargs.push_back(argv[0]);
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        size_t p = arg.find('=');
        if (p == std::string::npos)
            pargs.push_back(argv[i]);
        else {
            std::string param = arg.substr(0,p);
            std::string value = arg.substr(p+1);

            if (param == "out") {
                __ivy_out.open(value.c_str());
                if (!__ivy_out) {
                    std::cerr << "cannot open to write: " << value << std::endl;
                    return 1;
                }
            }
            else if (param == "iters") {
                test_iters = atoi(value.c_str());
            }
            else if (param == "runs") {
                runs = atoi(value.c_str());
            }
            else if (param == "seed") {
                seed = atoi(value.c_str());
            }
            else if (param == "delay") {
                sleep_ms = atoi(value.c_str());
            }
            else if (param == "wait") {
                final_ms = atoi(value.c_str());
            }
            else if (param == "modelfile") {
                __ivy_modelfile.open(value.c_str());
                if (!__ivy_modelfile) {
                    std::cerr << "cannot open to write: " << value << std::endl;
                    return 1;
                }
            }
            else {
                std::cerr << "unknown option: " << param << std::endl;
                return 1;
            }
        }
    }
    srand(seed);
    if (!__ivy_out.is_open())
        __ivy_out.basic_ios<char>::rdbuf(std::cout.rdbuf());
    argc = pargs.size();
    argv = &pargs[0];
    if (argc == 2){
        argc--;
        int fd = _open(argv[argc],0);
        if (fd < 0){
            std::cerr << "cannot open to read: " << argv[argc] << "\n";
            __ivy_exit(1);
        }
        _dup2(fd, 0);
    }
    if (argc != 1){
        std::cerr << "usage: test_v2 \n";
        __ivy_exit(1);
    }
    std::vector<std::string> args;
    std::vector<ivy_value> arg_values(0);
    for(int i = 1; i < argc;i++){args.push_back(argv[i]);}

#ifdef _WIN32
    // Boilerplate from windows docs

    {
        WORD wVersionRequested;
        WSADATA wsaData;
        int err;

    /* Use the MAKEWORD(lowbyte, highbyte) macro declared in Windef.h */
        wVersionRequested = MAKEWORD(2, 2);

        err = WSAStartup(wVersionRequested, &wsaData);
        if (err != 0) {
            /* Tell the user that we could not find a usable */
            /* Winsock DLL.                                  */
            printf("WSAStartup failed with error: %d\n", err);
            return 1;
        }

    /* Confirm that the WinSock DLL supports 2.2.*/
    /* Note that if the DLL supports versions greater    */
    /* than 2.2 in addition to 2.2, it will still return */
    /* 2.2 in wVersion since that is the version we      */
    /* requested.                                        */

        if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
            /* Tell the user that we could not find a usable */
            /* WinSock DLL.                                  */
            printf("Could not find a usable version of Winsock.dll\n");
            WSACleanup();
            return 1;
        }
    }
#endif
    for(int runidx = 0; runidx < runs; runidx++) {
    initializing = true;
    test_v2_repl ivy;
    for(unsigned i = 0; i < argc; i++) {ivy.__argv.push_back(argv[i]);}
    ivy._generating = false;

        ivy.__unlock();
        initializing = false;
        for(int rdridx = 0; rdridx < readers.size(); rdridx++) {
            readers[rdridx]->bind();
        }
                    
        init_gen my_init_gen;
        my_init_gen.generate(ivy);
        std::vector<gen *> generators;
        std::vector<double> weights;

        generators.push_back(new ext__protocol__fail_test_gen);
        weights.push_back(1.0);
        generators.push_back(new ext__protocol__idling_gen);
        weights.push_back(1.0);
        generators.push_back(new ext__protocol__update_r1_gen);
        weights.push_back(1.0);
        generators.push_back(new ext__protocol__update_r10_gen);
        weights.push_back(1.0);
        generators.push_back(new ext__protocol__update_r11_gen);
        weights.push_back(1.0);
        generators.push_back(new ext__protocol__update_r12_gen);
        weights.push_back(1.0);
        generators.push_back(new ext__protocol__update_r2_gen);
        weights.push_back(1.0);
        generators.push_back(new ext__protocol__update_r3_gen);
        weights.push_back(1.0);
        generators.push_back(new ext__protocol__update_r4_gen);
        weights.push_back(1.0);
        generators.push_back(new ext__protocol__update_r5_gen);
        weights.push_back(1.0);
        generators.push_back(new ext__protocol__update_r6_gen);
        weights.push_back(1.0);
        generators.push_back(new ext__protocol__update_r7_gen);
        weights.push_back(1.0);
        generators.push_back(new ext__protocol__update_r8_gen);
        weights.push_back(1.0);
        generators.push_back(new ext__protocol__update_r9_gen);
        weights.push_back(1.0);
        double totalweight = 14.0;
        int num_gens = 14;


#ifdef _WIN32
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
#endif
    for(int cycle = 0; cycle < test_iters; cycle++) {

        double choices = totalweight + readers.size() + timers.size();
        double frnd = choices * (((double)rand())/(((double)RAND_MAX)+1.0));
        if (frnd < totalweight) {
            int idx = 0;
            double sum = 0.0;
            while (idx < num_gens-1) {
                sum += weights[idx];
                if (frnd < sum)
                    break;
                idx++;
            }
            gen &g = *generators[idx];
            ivy.__lock();
#ifdef _WIN32
            LARGE_INTEGER before;
            QueryPerformanceCounter(&before);
#endif
            ivy._generating = true;
            bool sat = g.generate(ivy);
#ifdef _WIN32
            LARGE_INTEGER after;
            QueryPerformanceCounter(&after);
//            __ivy_out << "idx: " << idx << " sat: " << sat << " time: " << (((double)(after.QuadPart-before.QuadPart))/freq.QuadPart) << std::endl;
#endif
            if (sat){
                g.execute(ivy);
                ivy._generating = false;
                ivy.__unlock();
#ifdef _WIN32
                Sleep(sleep_ms);
#endif
            }
            else {
                ivy._generating = false;
                ivy.__unlock();
                cycle--;
            }
            continue;
        }


        fd_set rdfds;
        FD_ZERO(&rdfds);
        int maxfds = 0;

        for (unsigned i = 0; i < readers.size(); i++) {
            reader *r = readers[i];
            int fds = r->fdes();
            if (fds >= 0) {
                FD_SET(fds,&rdfds);
            }
            if (fds > maxfds)
                maxfds = fds;
        }

#ifdef _WIN32
        int timer_min = 15;
#else
        int timer_min = 1;
#endif

        struct timeval timeout;
        timeout.tv_sec = timer_min/1000;
        timeout.tv_usec = 1000 * (timer_min % 1000);

#ifdef _WIN32
        int foo;
        if (readers.size() == 0){  // winsock can't handle empty fdset!
            Sleep(timer_min);
            foo = 0;
        }
        else
            foo = select(maxfds+1,&rdfds,0,0,&timeout);
#else
        int foo = select(maxfds+1,&rdfds,0,0,&timeout);
#endif

        if (foo < 0)
#ifdef _WIN32
            {std::cerr << "select failed: " << WSAGetLastError() << std::endl; __ivy_exit(1);}
#else
            {perror("select failed"); __ivy_exit(1);}
#endif
        
        if (foo == 0){
            // std::cout << "TIMEOUT\n";            
           cycle--;
           for (unsigned i = 0; i < timers.size(); i++){
               if (timer_min >= timers[i]->ms_delay()) {
                   cycle++;
                   break;
               }
           }
           for (unsigned i = 0; i < timers.size(); i++)
               timers[i]->timeout(timer_min);
        }
        else {
            for (unsigned i = 0; i < readers.size(); i++) {
                reader *r = readers[i];
                if (FD_ISSET(r->fdes(),&rdfds))
                    r->read();
            }
        }            
    }
    
#ifdef _WIN32
                Sleep(final_ms);  // HACK: wait for late responses
#endif
    __ivy_out << "test_completed" << std::endl;
    for (unsigned i = 0; i < readers.size(); i++)
        delete readers[i];
    readers.clear();
    for (unsigned i = 0; i < timers.size(); i++)
        delete timers[i];
    timers.clear();


    }
    return 0;
}
