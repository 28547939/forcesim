
#include <cmath>

#include <stdio.h>
#include <iostream>

#include "Agent.h"



#include <random>

/*
#include <boost/math/distributions/uniform.hpp>
#include <boost/math/distributions/normal.hpp>
#include <boost/math/distributions/lognormal.hpp>

#include <boost/random.hpp>
#include <boost/random/random_device.hpp>
*/
#include <glog/logging.h>

TrivialAgent::TrivialAgent(AgentConfig<AgentType::Trivial> c) 
    : Agent_base<AgentType::Trivial>(c) {
}


/*
    "Trivial" agent takes the same action every time
*/
AgentAction TrivialAgent::do_evaluate(price_t p) {
    return {
        this->_config.direction,
        this->_config.internal_force
    };
}


BasicNormalDistAgent::BasicNormalDistAgent(AgentConfig<AgentType::BasicNormalDist> c) 
    : Agent_base<AgentType::BasicNormalDist>(c),
        dist(c.mean, c.stddev) {

        std::random_device dev;
        this->engine.seed(dev());

}


AgentAction BasicNormalDistAgent::do_evaluate(price_t p) {
    float internal_force = dist(this->engine);
    enum Direction dir;
    if (internal_force > 0) {
        if (internal_force > 100)
            internal_force = 100;
        dir = Direction::UP;
    } else {
        if (internal_force < -100)
            internal_force = -100;
        internal_force *= -1;
        dir = Direction::DOWN;
    }

    VLOG(10) << "BasicNormalDistAgent: direction=" << 
        (dir == Direction::UP ? "UP" : "DOWN") << " " << "internal_force=" << internal_force;

    return { dir, internal_force };
}


ModeledCohortAgent_v1::ModeledCohortAgent_v1(AgentConfig<AgentType::ModeledCohort_v1> c) 
    : Agent_base<AgentType::ModeledCohort_v1>(c),
        dist(0, c.initial_variance) 
{

        std::random_device dev;
        this->engine.seed(dev());

        this->price_view = c.default_price_view;


}

void ModeledCohortAgent_v1::info_update_view(Info::infoset_t& infoset) {

    for (auto& info_ptr : infoset) {
        Info::Types t = info_ptr->t();
        switch (t) {
            case Info::Types::Subjective: {
                auto i = Info::get_cast_throws<Info::Types::Subjective>(info_ptr);
                
                auto v =  this->_config.variance_multiplier * i->subjectivity_extent;
                this->dist = std::normal_distribution<double>(0, v);

                auto sample = this->dist(this->engine);

                double factor;
                if (sample >= 0) {
                    factor = 1 + sample;
                } else if (sample < 0) {
                    // samples x and -x should result in factors (1+x) and 1/(1+x)
                    factor = 1/( (sample * (-1)) + 1 );
                }

                if (i->is_relative) {
                } else {
                    this->price_view = i->price_indication * factor;
                    //std::cout << "price_view=" << static_cast<double>(this->price_view) << "\n";
                }

                break;
            }
            default:
            // ignore for now
            break;
        }
    }
}

AgentAction ModeledCohortAgent_v1::do_evaluate(price_t current_price) {

    while (auto is = this->read_next_infoset()) {
        // adjust price view based on incoming information
        this->info_update_view(is.value());
    }

    auto diff = (current_price - this->price_view).convert_to<long double>();

    long double internal_force = 
        std::min(1.0L, std::fabs(diff) / this->_config.force_threshold) * MAX_INTERNAL_FORCE;
    

    VLOG(9) << "ModeledCohortAgent_v1 debug:"
        << " diff=" << diff
        << " internal_force=" << internal_force
        << " price_view=" << this->price_view
        << " current_price=" << current_price
    ;


    return { 
        diff < 0 ? Direction::UP : Direction::DOWN,
        internal_force
    };

}
