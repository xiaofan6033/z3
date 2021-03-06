/*++
  Copyright (c) 2017 Microsoft Corporation

  Module Name:

  <name>

  Abstract:

  <abstract>

  Author:
  Nikolaj Bjorner (nbjorner)
  Lev Nachmanson (levnach)

  Revision History:


  --*/
#pragma once

#include "util/dependency.h"
#include "util/obj_hashtable.h"
#include "util/region.h"
#include "util/rlimit.h"
#include "util/statistics.h"
#include "math/dd/dd_pdd.h"

namespace dd {

class grobner {
public:
    struct stats {
        unsigned m_simplified;
        double   m_max_expr_size;
        unsigned m_max_expr_degree;
        unsigned m_superposed;
        unsigned m_compute_steps;
        void reset() { memset(this, 0, sizeof(*this)); }
        stats() { reset(); }
    };

    struct config {
        unsigned m_eqs_threshold;
        unsigned m_expr_size_limit;
        enum { basic, tuned } m_algorithm;
        config() :
            m_eqs_threshold(UINT_MAX),
            m_expr_size_limit(UINT_MAX),
                m_algorithm(tuned)
        {}
    };

    enum eq_state {
        solved,
        processed,
        to_simplify
    };

    class equation {
        eq_state                   m_state; 
        unsigned                   m_idx;        //!< unique index
        pdd                        m_poly;       //!< polynomial in pdd form
        u_dependency *             m_dep;        //!< justification for the equality
    public:
        equation(pdd const& p, u_dependency* d): 
            m_state(to_simplify),
            m_idx(0),
            m_poly(p),
            m_dep(d)
        {}

        const pdd& poly() const { return m_poly; }        
        u_dependency * dep() const { return m_dep; }
        unsigned idx() const { return m_idx; }
        void operator=(pdd const& p) { m_poly = p; }
        void operator=(u_dependency* d) { m_dep = d; }
        eq_state state() const { return m_state; }
        void set_state(eq_state st) { m_state = st; }
        void set_index(unsigned idx) { m_idx = idx; }
    };
private:

    typedef ptr_vector<equation> equation_vector;
    typedef std::function<void (u_dependency* d, std::ostream& out)> print_dep_t;

    pdd_manager&                                 m;
    reslimit&                                    m_limit;
    stats                                        m_stats;
    config                                       m_config;
    print_dep_t                                  m_print_dep;
    equation_vector                              m_solved; // equations with solved variables, triangular
    equation_vector                              m_processed;
    equation_vector                              m_to_simplify;
    mutable u_dependency_manager                 m_dep_manager;
    equation_vector                              m_all_eqs;
    equation*                                    m_conflict;   
public:
    grobner(reslimit& lim, pdd_manager& m);
    ~grobner();

    void operator=(print_dep_t& pd) { m_print_dep = pd; }
    void operator=(config const& c) { m_config = c; }

    void reset();
    void add(pdd const& p) { add(p, nullptr); }
    void add(pdd const& p, u_dependency * dep);

    void simplify();
    void saturate();

    equation_vector const& equations();
    u_dependency_manager& dep() const { return m_dep_manager;  }

    void collect_statistics(statistics & st) const;
    std::ostream& display(std::ostream& out, const equation& eq) const;
    std::ostream& display(std::ostream& out) const;

private:
    bool step();
    bool basic_step();
    bool basic_step(equation* e);
    equation* pick_next();
    bool canceled();
    bool done();
    void superpose(equation const& eq1, equation const& eq2);
    void superpose(equation const& eq);
    bool simplify_using(equation& eq, equation_vector const& eqs);
    bool simplify_using(equation_vector& set, equation const& eq);
    void simplify_using(equation & dst, equation const& src, bool& changed_leading_term);
    bool try_simplify_using(equation& target, equation const& source, bool& changed_leading_term);

    bool is_trivial(equation const& eq) const { return eq.poly().is_zero(); }    
    bool is_simpler(equation const& eq1, equation const& eq2) { return eq1.poly() < eq2.poly(); }
    bool is_conflict(equation const* eq) const { return is_conflict(*eq); }
    bool is_conflict(equation const& eq) const { return eq.poly().is_val() && !is_trivial(eq); }
    bool check_conflict(equation& eq) { return is_conflict(eq) && (set_conflict(eq), true); }    
    void set_conflict(equation& eq) { m_conflict = &eq; push_equation(solved, eq); }
    void set_conflict(equation* eq) { m_conflict = eq; push_equation(solved, eq); }
    bool is_too_complex(const equation& eq) const { return is_too_complex(eq.poly()); }
    bool is_too_complex(const pdd& p) const { return p.tree_size() > m_config.m_expr_size_limit;  }

    // tuned implementation
    vector<equation_vector> m_watch;           // watch list mapping variables to vector of equations where they occur (generally a subset)
    unsigned                m_levelp1;         // index into level+1
    unsigned_vector         m_level2var;       // level -> var
    unsigned_vector         m_var2level;       // var -> level

    bool tuned_step();
    void tuned_init();
    equation* tuned_pick_next();
    void simplify_watch(equation const& eq);
    void add_to_watch(equation& eq);

    void del_equation(equation& eq) { del_equation(&eq); }    
    void del_equation(equation* eq);    
    equation_vector& get_queue(equation const& eq);
    void retire(equation* eq) { dealloc(eq); }
    void pop_equation(equation& eq);
    void pop_equation(equation* eq) { pop_equation(*eq); }
    void push_equation(eq_state st, equation& eq);
    void push_equation(eq_state st, equation* eq) { push_equation(st, *eq); }

    struct compare_top_var;
    bool simplify_linear_step(bool binary);
    bool simplify_linear_step(equation_vector& linear);
    typedef vector<equation_vector> use_list_t;
    use_list_t get_use_list();
    void add_to_use(equation* e, use_list_t& use_list);
    void remove_from_use(equation* e, use_list_t& use_list);
    void remove_from_use(equation* e, use_list_t& use_list, unsigned except_v);

    bool simplify_cc_step();
    bool simplify_elim_pure_step();
    bool simplify_elim_dual_step();
    bool simplify_leaf_step();

    void invariant() const;
    struct scoped_process {
        grobner& g;
        equation* e;
        scoped_process(grobner& g, equation* e): g(g), e(e) {}
        ~scoped_process();        
    };

    void update_stats_max_degree_and_size(const equation& e);
    bool is_tuned() const { return m_config.m_algorithm == config::tuned;  }
    bool is_basic() const { return m_config.m_algorithm == config::basic; }
};

}
