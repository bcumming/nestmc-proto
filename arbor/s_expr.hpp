#pragma once

#include <arbor/arbexcept.hpp>

#include <cstddef>
#include <cstring>
#include <iterator>
#include <stdexcept>
#include <string>
#include <memory>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>

namespace arb {

// Exception types:
struct bad_s_expr_get: arbor_exception {
    bad_s_expr_get(const std::string& msg):
        arbor_exception("bad_s_expr_get: "+msg)
    {}
};

struct no_s_expr_token: arbor_internal_error {
    no_s_expr_token(): arbor_internal_error("no_s_expr_token") {}
};

struct bad_s_expr_access: arbor_exception {
    bad_s_expr_access(const std::string& msg):
        arbor_exception("bad_s_expr_access: "+msg)
    {}
};

struct bad_s_expr_tok2atom: arbor_exception {
    bad_s_expr_tok2atom(const std::string& msg):
        arbor_exception("bad_s_expr_tok2atom: "+msg)
    {}
};

// Forward iterator that can translate a raw stream to valid s_expr input if,
// perchance, you want to parse a half-hearted attempt at an s-expression
// (looking at you, the guy who invented the Neurolucida .asc format).
//
// I am not fond of .asc files, which would be s-expressions, if they
// didn't sometimes contain '|' and ',' characters which translate to ")(" and
// " " respectively (not mentioning the spine syntax).
//
// To remedy such situations, the transmogrifier performs user-provided string
// substitution on a characters in the input.
//
// For example, if you are unfortuinate enough to parse an asc file, you might want
// to try the following:
//
// transmogrifier(str, {{',', " "},
//                      {'|', ")("},
//                      {'<', "(spine "},
//                      {'>', ")"}});

class transmogrifier {
    using sub_map = std::unordered_map<char, std::string>;
    using iterator_type = std::string::const_iterator;
    using difference_type = std::string::difference_type;
    using iterator = transmogrifier;

    iterator_type pos_;
    iterator_type end_;
    sub_map sub_map_;

    const char* sub_pos_ = nullptr;

    void set_state() {
        sub_pos_ = nullptr;
        char next = *pos_;
        if (auto it=sub_map_.find(next); it!=sub_map_.end()) {
            sub_pos_ = it->second.c_str();
        }
    }

    public:

    transmogrifier(const std::string& s, sub_map map={}):
        pos_(s.cbegin()),
        end_(s.cend()),
        sub_map_(std::move(map))
    {
        if (pos_!=end_) {
            set_state();
        }
    }

    char operator*() const {
        if (pos_==end_) {
            return '\0';
        }
        if (sub_pos_) {
            return *sub_pos_;
        }
        return *pos_;
    }

    iterator& operator++() {
        // If already at the end don't advance.
        if (pos_==end_) {
            return *this;
        }

        // If currently substituting a string, advance by one and
        // test whether we have reached the end of the string.
        if (sub_pos_) {
            ++sub_pos_;
            if (*sub_pos_=='\0') { // test for end of string
                sub_pos_ = nullptr;
            }
            else {
                return *this;
            }
        }

        ++pos_;

        set_state();
        return *this;
    }

    iterator operator++(int) {
        iterator it = *this;

        ++(*this);

        return it;
    }

    iterator operator+(unsigned n) {
        iterator it = *this;

        while (n--) ++it;

        return it;
    }

    char peek(unsigned i) {
        return *(*this+i);
    }

    bool operator==(const transmogrifier& other) const {
        return pos_==other.pos_ && sub_pos_==other.sub_pos_;
    }

    bool operator!=(const transmogrifier& other) {
        return !(*this==other);
    }

    operator bool() const {
        return pos_ != end_;
    }

    difference_type operator-(const transmogrifier& rhs) const {
        return pos_ - rhs.pos_;
    }
};

struct src_location {
    unsigned line = 0;
    unsigned column = 0;

    src_location() = default;

    src_location(unsigned l, unsigned c):
        line(l), column(c)
    {}
};

std::ostream& operator<<(std::ostream& o, const src_location& l);

enum class tok {
    nil,
    real,       // real number
    integer,    // integer
    symbol,     // symbol
    lparen,     // left parenthesis '('
    rparen,     // right parenthesis ')'
    string,     // string, written as "spelling"
    eof,        // end of file/input
    error       // special error state marker
};

std::ostream& operator<<(std::ostream&, const tok&);

// Token generated when lexing for s-expressions

struct token {
    src_location loc;
    tok kind;
    std::string spelling;
};
std::ostream& operator<<(std::ostream&, const token&);

// Atom types

struct s_expr_symbol {
    std::string str;
    operator std::string() const { return str; }
    bool friend operator< (const s_expr_symbol& lhs, const s_expr_symbol& rhs) { return lhs.str<rhs.str; }
    bool friend operator==(const s_expr_symbol& lhs, const s_expr_symbol& rhs) { return lhs.str==rhs.str; }
};

struct s_expr_error {
    std::string str;
    operator std::string() const { return str; }
};

struct nil_t {};

using atom = std::variant<double, long long, std::string, s_expr_symbol, nil_t, s_expr_error>;
std::ostream& operator<<(std::ostream&, const atom&);

atom to_atom(const token& t);

namespace s_expr_literals {
    inline s_expr_symbol operator "" _symbol(const char* chars, size_t size) {
        return {chars};
    }
}

struct s_expr {
    template <typename U>
    struct s_pair {
        U head = U();
        U tail = U();
        s_pair(U l, U r): head(std::move(l)), tail(std::move(r)) {}
    };

    // This value_wrapper is used to wrap the shared pointer
    template <typename T>
    struct value_wrapper{
        using state_t = std::unique_ptr<T>;
        state_t state;

        value_wrapper() = default;

        value_wrapper(const T& v):
            state(std::make_unique<T>(v)) {}

        value_wrapper(T&& v):
            state(std::make_unique<T>(std::move(v))) {}

        value_wrapper(const value_wrapper& other):
            state(std::make_unique<T>(other.get())) {}

        value_wrapper& operator=(const value_wrapper& other) {
            state = std::make_unique<T>(other.get());
            return *this;
        }

        value_wrapper(value_wrapper&& other) = default;

        friend std::ostream& operator<<(std::ostream& o, const value_wrapper& w) {
            return o << *w.state;
        }

        operator T() const {
            return *state;
        }

        const T& get() const {
            return *state;
        }

        T& get() {
            return *state;
        }
    };

    // An s_expr can be one of
    //      1. an atom
    //      2. a pair of s_expr (head and tail)
    // The s_expr uses a util::variant to represent these two possible states,
    // which requires using an incomplete definition of s_expr, requiring
    // with a std::unique_ptr via value_wrapper.

    using cons = s_pair<value_wrapper<s_expr>>;
    std::variant<atom, cons> state_ = nil_t{};// token{{0,0}, tok::nil, "nil"};
    std::shared_ptr<token> token_;

    s_expr(const s_expr& s) = default;
    s_expr() = default;
    s_expr(const token& t): state_(to_atom(t)), token_(std::make_shared<arb::token>(std::move(t))) {}
    s_expr(s_expr l, s_expr r):
        state_(cons(std::move(l), std::move(r)))
    {}

    bool is_atom() const;

    const token& tok() const;
    const atom& as_atom() const;

    operator bool() const;

    const s_expr& head() const;
    const s_expr& tail() const;
    s_expr& head();
    s_expr& tail();

    bool is_error() const { return is_atom() ? std::holds_alternative<s_expr_error>(atom()): false; }

    friend std::ostream& operator<<(std::ostream& o, const s_expr& x);
};

struct list_adaptor {
    const s_expr& expression_;
    list_adaptor(const s_expr& e): expression_(e) {};

    template <bool Const>
    class s_expr_iterator_impl {
        public:

        struct sentinel {};

        using value_type = s_expr;
        using iterator_category = std::forward_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using pointer   = std::conditional_t<Const, const s_expr*, s_expr*>;
        using reference = std::conditional_t<Const, const s_expr&, s_expr&>;

        s_expr_iterator_impl(reference e):
            inner_(&e)
        {
            if (inner_->is_atom()) {
                throw std::runtime_error("Attempt to create s_expr_iterator on an atom.");
            }
            if (finished()) inner_ = nullptr;
        }

        s_expr_iterator_impl(const sentinel& e):
            inner_(nullptr)
        {}

        reference operator*() const {
            return inner_->head();
        }

        pointer operator->() const {
            return &inner_->head();
        }

        s_expr_iterator_impl& operator++() {
            advance();
            return *this;
        }

        s_expr_iterator_impl operator++(int) {
            s_expr_iterator_impl cur = *this;
            advance();
            return cur;
        }

        s_expr_iterator_impl operator+(difference_type i) const {
            s_expr_iterator_impl it = *this;
            while (i--) {
                ++it;
            }
            return it;
        }
        bool operator==(const s_expr_iterator_impl& other) const {
            return inner_==other.inner_;
        }
        bool operator!=(const s_expr_iterator_impl& other) const {
            return !(*this==other);
        }
        bool operator==(const sentinel& other) const {
            return !inner_;
        }
        bool operator!=(const sentinel& other) const {
            return !(*this==other);
        }

        reference expression() const {
            return *inner_;
        }

        private:

        bool finished() const {
            // Finished if reached nil_t
            return !*inner_;
        }

        void advance() {
            if (!inner_) return;
            inner_ = &inner_->tail();
            if (finished()) inner_ = nullptr;
        }

        // Pointer to the current s_expr.
        // Set to nullptr when at the end of the range.
        pointer inner_;
    };

    //using iterator       = s_expr_iterator_impl<false>;
    using const_iterator = s_expr_iterator_impl<true>;

    //iterator       begin()        { return {*this}; }
    //iterator       end()          { return iterator::sentinel{}; }
    const_iterator begin()  const { return expression_; }
    const_iterator end()    const { return const_iterator::sentinel{}; }
    const_iterator cbegin() const { return expression_; }
    const_iterator cend()   const { return const_iterator::sentinel{}; }
};

src_location location(const s_expr& l);

s_expr parse_s_expr(const std::string& line);
s_expr parse_s_expr(transmogrifier begin);
std::vector<s_expr> parse_multi(transmogrifier begin);

template <typename T>
T get(const s_expr& e);

template <> double get<double>(const s_expr&);
template <> int get<int>(const s_expr&);
template <> std::string get<std::string>(const s_expr&);
template <> s_expr_symbol get<s_expr_symbol>(const s_expr&);
template <> s_expr_error get<s_expr_error>(const s_expr&);

bool is_numeric(const s_expr& e);
bool is_real(const s_expr& e);
bool is_integral(const s_expr& e);
bool is_error(const s_expr& e);
bool is_symbol(const s_expr& e);
bool is_string(const s_expr& e);
bool is_nil(const s_expr& e);


} // namespace arb

