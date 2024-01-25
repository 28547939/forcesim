#ifndef INFO_H
#define INFO_H 1


#include <map>
#include <string>
#include <memory>
#include <deque>
#include <optional>
#include <set>

#include "types.h"

namespace Info {

    enum class Types {
        Test1,
        Objective,
        Subjective
    };

    struct Abstract {
        protected:
        static const Types type;
        public:
        //Abstract() = delete;
        virtual Types t() = 0;
    };

    typedef std::set<std::shared_ptr<Abstract>> infoset_t;

    template<enum Types T>
    struct Base : public Abstract {
        private:
        static const Types type = T; 
        public:
        //Base() = delete;
        Types t() final { return this->type; } 
        virtual bool is_valid() = 0;
    };

    /*  the derived classes */
    template<enum Types> struct Info {};

    template<>
    struct Info<Types::Test1> : public Base<Types::Test1> {
        float item1;

        virtual bool is_valid() { return true; }
    };

    template<>
    struct Info<Types::Subjective> : public Base<Types::Subjective> {
        /* must be in [0, 100]
            A value of 0 means "completely objective", 100 means "completely subjective"
        */
        float subjectivity_extent;
        price_t price_indication;

        // Whether the price indication is a "premium", i.e. should be taken
        // as an offset/premium relative to other non-relative indications

        // For example, we can emit an Info struct that gives an absolute
        // (non-relative) price indication as a base, and then supplement the indication
        // given in that struct with additional relative indications such as risk 
        // premia, which might have different values for subjectivity_extent.
        bool is_relative;

        virtual bool is_valid() {
            return (
                (this->subjectivity_extent >= 0 && this->subjectivity_extent <= 100) &&
                (this->is_relative == true ? true : (this->price_indication >= 0))
            );
        }
    };

     
    // Assuming all Info types correctly inherit from the corresponding Base,
    // specifically that Info<T> always inherits from Base<T>,
    // get_cast is guaranteed to return a subtyped object (i.e. perform the cast)
    // only when the underlying object is in fact of that subtype
    template<enum Types T>
    std::optional<std::shared_ptr< Info<T> >>
    get_cast(std::shared_ptr<Abstract> ptr) {
        if (ptr->t() == T) {
            return dynamic_pointer_cast<Info<T>>(ptr);
        }
        return std::nullopt;
    }

    template<enum Types T>
    std::shared_ptr< Info<T> >
    get_cast_throws(std::shared_ptr<Abstract> ptr) {
        return *(get_cast<T>(ptr));
    }



/*
TODO not going with this design
    typedef std::variant<
        Info<Types::Subjective>,
        Info<Types::Test1>
    > var_t;

    var_t as_variant(std::shared_ptr<Abstract> ptr) {
        switch
    }
    */


};

#endif