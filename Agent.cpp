
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

double normalsample_to_factor(double sample) {
    double factor;
    if (sample >= 0) {
        factor = 1 + sample;
    } else if (sample < 0) {
        // samples x and -x should result in factors (1+x) and 1/(1+x)
        factor = 1/( (sample * (-1)) + 1 );
    }

    return factor;
}

void ModeledCohortAgent_v1::info_update_view(std::shared_ptr<Info::Info<Info::Types::Subjective>>& i) {
    auto v =  this->_config.variance_multiplier * i->subjectivity_extent;
    this->dist = std::normal_distribution<double>(0, v);

    auto sample = this->dist(this->engine);

    double factor = normalsample_to_factor(sample);

    if (i->is_relative) {
    } else {
        this->price_view = i->price_indication * factor;
        //std::cout << "price_view=" << static_cast<double>(this->price_view) << "\n";
    }
}

void ModeledCohortAgent_v1::info_handler() {

    while (auto is = this->read_next_infoset()) {
        // adjust price view based on incoming information

        for (auto& info_ptr : is.value()) {
            Info::Types t = info_ptr->t();
            switch (t) {
                case Info::Types::Subjective: {
                    auto i = Info::get_cast_throws<Info::Types::Subjective>(info_ptr);
                    this->info_update_view(i);
                break;
                }
                default:
                // ignore for now
                break;
            }
        }
    }

    
}

AgentAction ModeledCohortAgent_v1::do_evaluate(price_t current_price) {
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

ModeledCohortAgent_v2::ModeledCohortAgent_v2(AgentConfig<AgentType::ModeledCohort_v2> c) 
    :   ModeledCohortAgent_v1(c),
        config(c)
{}

/*

r_0     y-value of lower segment (around current price)

*/

std::pair<std::deque<double>, std::deque<double>>
ModeledCohortAgent_v2::compute_distribution_points(
        price_t price, std::optional<float> override_subjectivity_extent) 
    {


    float s = override_subjectivity_extent.value_or(this->current_subjectivity_extent);
    auto i_0 = this->config.i_0;
    auto i_1 = this->config.i_1;
    auto i_2 = this->config.i_1;
    auto r_0 = this->config.r_0;
    auto r_1 = this->config.r_1;
    auto r_2 = this->config.r_2;
    auto e_0 = this->config.e_0;
    auto e_1 = this->config.e_1;
    auto e_2 = this->config.e_2;
    auto v = this->price_view.convert_to<double>();
    auto c = price.convert_to<double>();

    auto d = std::fabs(v - c);

    std::vector<double> segments = {
        e_0*s*d,
        i_0*s*d,
        (d - i_1*s*d)*(1-r_1)*s,
        (d - i_1*s*d)*(r_1)*s,
        i_1*s*d,
        i_2*s*d,
        e_2*s*d
    };

    auto x_2 = c;
    auto x_1 = x_2 - i_0*s*d;
    auto x_5 = v;
    auto x_4 = x_5 - i_1*s*d;
    auto x_6 = v + i_2*s;


    std::deque<double> xs;

    if (this->price_view > price) {
        xs.push_back(c - segments[1] - segments[0]);
    } else {
        std::reverse(segments.begin(), segments.end());
        xs.push_back(v + segments[1] + segments[0]);
    }

    
    auto prev = xs.begin();
    for (auto s = segments.begin(); s != segments.end(); ++s, ++prev) {
        xs.push_back(
            *prev + (*s * (this->price_view > price ? 1 : -1))
        );
    }

/*

    if (this->price_view > price) {

        xs = { 
            x_1 - e_0*s*d,
            x_1,
            x_2,
            x_4 - (d - i_1*s*d)*r_1*s,
            x_4,
            x_5,
            x_6,
            x_6 + e_2*s,
        };
    } else if (this->price_view < price) {
        xs = { 
            v - i_2*s - e_2*s,
            v - i_2*s,
            v
            v + i_1*s,
            (c - (v + i_1*s))*r_1*s - (v + i_1*s)
            v,
            c + i_0,
            c + e_0 + i_0,
        };
    } else {
        return { {}, {} };
    }
    */

    std::deque<double> ys = {
        0,
        r_0*s, 
        r_0*s,
        r_2*s,
        1,
        1, 
        1, 
        0
    };


    return { xs, ys };
}

AgentAction ModeledCohortAgent_v2::do_evaluate(price_t current_price) {

    this->info_handler();

    auto [xs, ys] = this->compute_distribution_points(current_price);

    // construct piece-wise linear distribution to combine "price inertia" with the 
    // subjective price view
    std::piecewise_linear_distribution<double> dist(xs.begin(), xs.end(), ys.end());
    double attraction_point = dist(this->engine);


    auto diff = (current_price - attraction_point).convert_to<long double>();

    long double internal_force = 
        std::min(1.0L, std::fabs(diff) / this->_config.force_threshold) * MAX_INTERNAL_FORCE;
    

    VLOG(9) << "ModeledCohortAgent_v2 debug:"
        << " diff=" << diff
        << " internal_force=" << internal_force
        << " price_view=" << this->price_view
        << " attraction_point=" << attraction_point
        << " current_price=" << current_price
    ;


    return { 
        diff < 0 ? Direction::UP : Direction::DOWN,
        internal_force
    };

}