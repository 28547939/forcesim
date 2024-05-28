
#include <cmath>

#include <stdio.h>
#include <iostream>

#include "Agent.h"

#include <random>

#include <glog/logging.h>

namespace Agent {

Agent_impl<AgentType::Trivial>::Agent_impl(AgentConfig<AgentType::Trivial> c) 
    : Agent_base<AgentType::Trivial>(c), Agent(c) {
}


AgentAction TrivialAgent::do_evaluate(price_t p) {
    return {
        this->_config.direction,
        this->_config.internal_force
    };
}


Agent_impl<AgentType::BasicNormalDist>::Agent_impl(AgentConfig<AgentType::BasicNormalDist> c) 
    : Agent_base<AgentType::BasicNormalDist>(c), Agent(c), 
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


Agent_impl<AgentType::ModeledCohort_v1>::Agent_impl(AgentConfig<AgentType::ModeledCohort_v1> c) 
    : Agent_base<AgentType::ModeledCohort_v1>(c), Agent(c),
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

Agent_impl<AgentType::ModeledCohort_v2>::Agent_impl(AgentConfig<AgentType::ModeledCohort_v2> c) 
    :   ModeledCohortAgent_v1(c), Agent_base<AgentType::ModeledCohort_v2>(c), Agent(c)
{}


std::tuple<std::deque<double>, std::deque<double>,
    std::optional<std::tuple<
        std::deque<std::string>,    // segment labels
        std::deque<std::string>,          // y labels
        std::deque<double>          // segment values
    >>
>
ModeledCohortAgent_v2::compute_distribution_points(
        price_t price, std::optional<float> override_subjectivity_extent, bool trace
    ) {

    float s = override_subjectivity_extent.value_or(this->current_subjectivity_extent);

    // the piecewise distribution is not applicable at s=0
    if (s == 0) {
        return { {}, {}, std::nullopt };
    }

    // we have two Agent_base bases - one we inherit from directly, one inherited by 
    // our based ModledCohort_v1
    auto config = this->Agent_base<AgentType::ModeledCohort_v2>::config();

    auto e_0 = config.e_0;
    auto i_0 = config.i_0;
    auto r_0 = config.r_0;
    auto r_1 = config.r_1;
    auto r_2 = config.r_2;
    auto i_1 = config.i_1;
    auto i_2 = config.i_2;
    auto e_1 = config.e_1;
    auto v = this->price_view.convert_to<double>();
    auto c = price.convert_to<double>();

    // the 'width'/'breadth' of the distribution is scaled depending on the difference 
    // between our view and the current price 
    auto d = std::fabs(v - c);

    std::deque<double> ys = {
        0,
        s,
        s,              // current price

        // i.e. the "r_2 midpoint" b/w the adjacent y points
        std::fmin(r_0*(1-s), s) + std::fabs(r_0*(1-s) - s) * r_2,
        // subjectivity does not factor into it w.r.t y - did not think of a consistent way to do that

        r_0*(1-s),
        r_0*(1-s),      // price view
        r_0*(1-s),      
        0
    };

    std::deque<double> segments = {
        e_0*s*d,
        i_0*s*d,

        // sum of these two is always (d - i_1*s*d)
        (d - i_1*s*d)*(1-r_1*s),   
        (d - i_1*s*d)*(r_1*s),      

        i_1*s*d,
        i_2*s*d,
        e_1*s*d
    };


    std::deque<std::string> segment_labels = {
        "e_0*s*d",
        "i_0*s*d",
        "(d - i_1*s*d)*(r_1*s)",
        "(d - i_1*s*d)*(1-r_1*s)",
        "i_1*s*d",
        "i_2*s*d",
        "e_1*s*d"
    };

    std::deque<std::string> y_labels = {
        "0",
        "s",
        "s",
        "min(r_0*(1-s), s)+|r_0*(1-s)-s)|*r_2",
        "r_0*(1-s)",
        "r_0*(1-s)", 
        "r_0*(1-s)", 
        "0"
    };


    // begin assembling the points of the distribution, with possible duplicates (in the case of
    // segments of zero length), to be resolved later
    std::map<double, std::deque<double>> pts_multi;

    // initialize with the first point 
    if (this->price_view > price) {
        pts_multi.insert(pts_multi.end(), {
            c - segments[1] - segments[0],
            { ys[0] }
        });
    } else {
        std::reverse(segments.begin(), segments.end());
        std::reverse(ys.begin(), ys.end());

        pts_multi.insert(pts_multi.end(), {
            v - segments[1] - segments[0],
            { *( ys.end() - 1 ) }
        });
    }

    
    auto prev = pts_multi.begin();
    auto next_y = std::next(ys.begin());
    for (auto s = segments.begin(); s != segments.end(); ++s, ++next_y) {
        // current segment value, to be either added or subtracted depending on which
        // direction we are building the graph; possibly 0, in which case the pts_multi
        // value will have more than one element
        auto increment = (*s * (this->price_view > price ? 1 : -1));
        auto new_x = (*prev).first + increment;

        auto existing = pts_multi.find(new_x);

        // there is a duplicate x-value
        if (existing != pts_multi.end()) {
            existing->second.push_back(*next_y);
        } 
        // not a duplicate - this is a new x value
        else {
            pts_multi.insert(pts_multi.end(), { new_x, { *next_y } });
            // only increment our iterator if we actually inserted a new value
            prev = std::prev(pts_multi.end());
        }

    }

    // next, "collapse" / "consolidate" points with identical x coordinates together to avoid
    // providing inconsistent data to the distribution (i.e. each x value needs exactly
    // one y value - it needs to be a (math) function)

    // track our position in the original, non-consolidated list of points
    int i = 0;

    // indices (relative to the non-consolidated list of points) that take priority when
    // an x value has multiple y values
    std::set<int> immutable = { 2, 5 };

    std::deque<double> xs_final;
    std::deque<double> ys_final;
    for (auto pts_it = pts_multi.begin(); pts_it != pts_multi.end(); ++pts_it) {
        auto x = pts_it->first;
        auto& y_multi = pts_it->second;
        std::optional<double> y_final;

        xs_final.push_back(x);

        // if there are no duplicate y values for this x, just add the value
        if (y_multi.size() == 1) {
            y_final = y_multi.at(0);
        }

        // duplicate y values - we need to choose the immutable one
        else {
            for (auto j : immutable) {
                // if the immutable index lies within the current range of x points
                // (in terms of insertion order in the non-consolidated list of points), 
                // it takes priority
                //
                // (if everything is working correctly, the y value at j is present in 
                // y_multi, but it's easier to read it directly from the original ys array)
                if (i <= j && j <= i + y_multi.size()) {
                    y_final = ys[j];
                }
            }

            // if there are duplicate y values that do not overlap at all with an 'immutable' 
            // index
            // the rule is to preserve whatver is closer to the 'center' of the distribution (so
            // away from the edge). for now this is the same as the highest y value, but this could
            // change if we redesign the distribution.
            if (!y_final.has_value()) {
                y_final = *( std::max_element(y_multi.begin(), y_multi.end()) );

                /* in case we need to return info about which index was kept - not currently needed
                auto y_final_it = std::max_element(y_multi.begin(), y_multi.end());
                y_final = *y_final_it;
                size_t relative_index = std::distance(y_final_it, y_multi.begin());
                */
            }
        }

        ys_final.push_back(y_final.value());

        // next index in the original, non-consolidated list of points depends on how many 
        // non-consolidated y values we have just processed
        i += y_multi.size();
    }

    std::optional<std::tuple<
        std::deque<std::string>,    // segment labels
        std::deque<std::string>,          // y labels
        std::deque<double>          // segment values
    >> trace_output;

    if (trace == true) {
        trace_output.emplace(
            segment_labels, y_labels, segments
        );
    }

    return { xs_final, ys_final, trace_output };
}


void ModeledCohortAgent_v2::info_update_view(std::shared_ptr<Info::Info<Info::Types::Subjective>>& i) {
    this->ModeledCohortAgent_v1::info_update_view(i);

    this->current_subjectivity_extent = i->subjectivity_extent;
}

AgentAction ModeledCohortAgent_v2::do_evaluate(price_t current_price) {

    this->info_handler();
    auto config = this->Agent_base<AgentType::ModeledCohort_v2>::_config;

    double attraction_point;

    if (this->current_subjectivity_extent > 0) {
        auto [xs, ys, trace] = this->compute_distribution_points(current_price);

        // construct piece-wise linear distribution to combine "price inertia" with the 
        // subjective price view
        std::piecewise_linear_distribution<double> dist(xs.begin(), xs.end(), ys.begin());
        attraction_point = dist(this->engine);
    } 

    // 
    else {
        attraction_point = this->price_view.convert_to<double>();
    }


    auto diff = (current_price - attraction_point).convert_to<long double>();

    long double internal_force = 
        std::min(1.0L, std::fabs(diff) / config.force_threshold) * MAX_INTERNAL_FORCE;
    

    VLOG(9) << "ModeledCohortAgent_v2 debug:"
        << " diff=" << diff
        << " internal_force=" << internal_force
        << " price_view=" << this->price_view
        << " attraction_point=" << attraction_point
        << " current_price=" << current_price
        << " current_subjectivity_extent=" << current_subjectivity_extent
    ;


    return { 
        diff < 0 ? Direction::UP : Direction::DOWN,
        internal_force
    };

}

}