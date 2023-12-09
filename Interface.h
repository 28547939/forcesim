
#include <crow.h>
#include "Market.h"
#include "json_conversion.h"

#include <functional>
#include <optional>


using json = nlohmann::json;

class Interface;

// global error codes that we can use to communicate a specific error condition in 
// a response without requiring the client to match on the message
// strings are used instead of numbers to make the errors clear in case a user is
// reading the raw response
enum class InterfaceErrorCode {
    General_error,
    Json_parse_error,
    Json_type_error,
    Multiple,
    Already_started,
    Not_found,

    Agent_not_implemented,
    Agent_config_error,

    Subscriber_config_error,
};

enum class InterfaceResponseType {
    Data,
    Multiple_stringmap,
    Multiple_pairlist,
    Multiple_barelist
};

inline std::map<enum InterfaceErrorCode, std::string> interface_error_code_str({
    { InterfaceErrorCode::General_error, "General_error" },
    { InterfaceErrorCode::Json_parse_error, "Json_parse_error" },
    { InterfaceErrorCode::Json_type_error, "Json_type_error" },
    { InterfaceErrorCode::Multiple, "Multiple" },
    { InterfaceErrorCode::Already_started, "Already_started" },
    { InterfaceErrorCode::Not_found, "Not_found" },
    { InterfaceErrorCode::Agent_not_implemented, "Agent_not_implemented" },
    { InterfaceErrorCode::Agent_config_error, "Agent_config_error" },
    { InterfaceErrorCode::Subscriber_config_error, "Subscriber_config_error" },
});

inline std::map<enum InterfaceResponseType, std::string> interface_response_type_str({
    { InterfaceResponseType::Data, "Data" },
    { InterfaceResponseType::Multiple_stringmap, "Multiple_stringmap" },
    { InterfaceResponseType::Multiple_pairlist, "Multiple_pairlist" },
    { InterfaceResponseType::Multiple_barelist, "Multiple_barelist" },
});

inline void to_json(json& j, const enum InterfaceErrorCode x) {
    j = interface_error_code_str[x];
}

inline void to_json(json& j, const enum InterfaceResponseType x) {
    j = interface_response_type_str[x];
}

template<typename RetKey>
struct detect_multi_response_type {
    static const inline enum InterfaceResponseType value = InterfaceResponseType::Multiple_pairlist;
};

template<>
struct detect_multi_response_type<int> {
    static const inline enum InterfaceResponseType value = InterfaceResponseType::Multiple_barelist;
};

template<>
struct detect_multi_response_type<std::string> {
    static const inline enum InterfaceResponseType value = InterfaceResponseType::Multiple_stringmap;
};


/********************************************************* 
 * "list request" processing 
 * 
 * 
 * when an incoming request is a "multi-request", in the sense of providing a list an endpoint
 * interprets as a list of requests, there is a certain amount of boilerplate involved in the 
 * application of the endpoint's logic to each element (request) in that list and assembling the
 * results into a list (or more generally, a map, possibly with integer indices) for the response.
 * 
 * endpoint methods (Interface::crow__*) call either list_generator_helper or list_handler_helper,
 * which both call the main helper function, list_helper:
 * 
 *  template<typename InputItem, typename RetKey, typename RetVal, typename HandlerType>
 *  static void list_helper(const crow::request& req, crow::response& res, 
 *       HandlerType handler_f, 
 *       std::optional<std::function<void(int)>> finally_f = std::nullopt);
 * 
 * list_helper does the following:
 * - parse request JSON
 * - interpret the JSON as an array of items of type InputItem (see above)
 * - run the function of type HandlerType on the input, which assembles them in a result
 *      structure of the appropriate type (depending on RetKey and RetValue)
 * - optionally run a "finally" function after  all the input is processed
 * - convert the assembled results to JSON and build the response
 * 
 * list_helper uses an "adapter" (list_helper_adapter) to run the HandlerType handler, 
 *  which takes care of both running handler_f and also assembling the results in the result structure.
 * For each adapter, there is exactly one HandlerType type: the list_helper_adapter struct 
 *  template is specialized by HandlerType.
 *  
 * 
 * Currently, HandlerType can be one of:
 * - list_helper_generator_t (used by list_generator_helper)
 * - list_helper_handler_t (used by list_handler_helper)
 * 
 * TODO further documentation
  ******************************************************** */

using list_error_t = std::tuple<InterfaceErrorCode, std::string>;

template<typename T>
using list_ret_t = std::variant<T, list_error_t>;

template<typename K, typename V>
using list_retmap_t = std::map<K, list_ret_t<V>>;


template<typename InputItem, typename RetKey, typename RetVal, typename HandlerType>
struct list_helper_adapter;

// type of inner request handler which is executed (and returns a value) for each element 
// in the list provided in the request
// data structure returned to HTTP client is a 
//  map<int, list_ret_t<RetVal>> ( = list_retmap_t<int>)
// where each value in the map is the value returned from an individual invocation of the 
//  handler
template<typename InputItem, typename RetVal>
using list_helper_generator_t = 
    std::function<list_ret_t<RetVal>(std::shared_ptr<Interface>, InputItem&)>;

// specialization for list_helper_generator_t
// TODO switch to allow returning items in their place in the array vs as map keys
template<typename InputItem, typename RetKey, typename RetVal>
struct list_helper_adapter<
    InputItem, RetKey, RetVal, 
    list_helper_generator_t<InputItem, RetVal>
> {

    void operator()
    (   list_helper_generator_t<InputItem, RetVal> f,
        std::shared_ptr<Interface> interface,
        std::deque<InputItem>& input,
        list_retmap_t<RetKey, RetVal>&  retmap
    )
    {
        auto it = input.begin();

        for (std::size_t i = 0; it != input.end(); i++, it++) {
            retmap.insert(retmap.end(), { i, f(interface, *it) });
        }
    }
};



// type of inner request handler which is executed once, being passed the entire list that
//  was provided in the request
// data structure returned to HTTP client is a 
//  map<RetKey, list_ret_t<RetVal>> ( = list_retmap_t<RetKey>)
// where this map is the value returned by the handler
template<typename InputItem, typename RetKey, typename RetVal>
using list_helper_handler_t = 
    std::function<list_retmap_t<RetKey, RetVal>(std::shared_ptr<Interface>, std::deque<InputItem>&)>;

// specialization for list_helper_handler_t
template<typename InputItem, typename RetKey, typename RetVal>
struct list_helper_adapter<
    InputItem, RetKey, RetVal, 
    list_helper_handler_t<InputItem, RetKey, RetVal>>
    {

    void operator()
    (   list_helper_handler_t<InputItem, RetKey, RetVal> f,
        std::shared_ptr<Interface> interface,
        std::deque<InputItem>& input,
        list_retmap_t<RetKey, RetVal>& retmap
    )
    {
        retmap = f(interface, input);
    }
};



// 2023-04-07 change
// list ret needs to be a map instead of list

// singleton
class Interface : public std::enable_shared_from_this<Interface> {
    protected:
    std::shared_ptr<Market::Market> market;
    crow::SimpleApp crow_app;


    static void 
    handle_json_wrapper(
        const crow::request&, crow::response&,
        std::function<void(const crow::request& req, crow::response& res, json&)>
    );

    static void 
    handle_json_array_wrapper(
        const crow::request&, crow::response&,
        std::function<void(const crow::request& req, crow::response& res, std::deque<json>&)>
    );
    
    
    static std::optional<json>
    handle_json(const crow::request&, crow::response&);

    static std::optional<std::deque<json>>
    handle_json_array(const crow::request&, crow::response&);


    Interface(std::shared_ptr<Market::Market>);
    inline static bool instantiated = false;
    inline static std::shared_ptr<Interface> instance;


    /*
        It's easier for the handlers to be static, because passing them as handlers (ie inline) to 
        the CROW_ROUTE macro when they are non-static function pointers / methods does
        not appear to be possible with std::bind.
        Passing them as plain function pointers without std::bind and without a bound `this`
        works as expected. 
        Interface works well as a "singleton" class, so this design is not inconvenient.

        These must not be called until the Interface singleton is instantiated, since they
        access the instance pointer (Interface::instance)
    */

    static void crow__market_run(const crow::request& req, crow::response&);
    static void crow__market_stop(const crow::request&, crow::response&);
    static void crow__market_wait_for_stop(const crow::request&, crow::response&);

    static void crow__market_configure(const crow::request&, crow::response&);
    static void crow__market_start(const crow::request& req, crow::response& resp);

    static void crow__market_reset(const crow::request& req, crow::response& resp);

    static void crow__get_price_history(const crow::request&, crow::response&);



    // final argument: "finally" function to execute after results have been assembled;
    // the provided argument to the function is number of entries in the result
    template<typename InputItem, typename RetKey, typename RetVal, typename HandlerType>
    static void list_helper(const crow::request& req, crow::response& res, 
        HandlerType, 
        std::optional<std::function<void(int)>> = std::nullopt);


    template<typename InputItem, typename RetVal>
    static void list_generator_helper(const crow::request& req, crow::response& res, 
        list_helper_generator_t<InputItem, RetVal> f,
        std::optional<std::function<void(int)>> finally_f = std::nullopt)
    {
        Interface::list_helper<InputItem, int, RetVal, list_helper_generator_t<InputItem, RetVal>>
            (req, res, f, finally_f);
    }

    template<typename InputItem, typename RetKey, typename RetVal>
    static void list_handler_helper(const crow::request& req, crow::response& res, 
        list_helper_handler_t<InputItem, RetKey, RetVal> f,
        std::optional<std::function<void(int)>> finally_f = std::nullopt)
    {
        Interface::list_helper<InputItem, RetKey, RetVal, list_helper_handler_t<InputItem, RetKey, RetVal>>
            (req, res, f, finally_f);
    }


    /*

        POST /agent/add
        [
            {   "type": (value of type enum AgentType)
                "count": (integer: number of agents to create with this config)
                "config": (JSON form of an AgentConfig class (see Agent.h))
            }
        ]

        Returns: {
            "timepoint": current Market::timept (timepoint_t) value, when the agents were created
            "ids": [ 
                list of IDs for each entry in the request
            ]
        }
    */
    static void crow__add_agents(const crow::request&, crow::response&);


	/*
		POST /agent/del
		[ id1, id2, ... ]

		Returns: {
            id: (bool value - whether the deletion was successful)
        }

	*/
    static void crow__del_agents(const crow::request&, crow::response&);



	/*
		GET /agent/list

        Optional parameter: timepoint (show only agents created at the given timepoint_t value)
		
        Returns: more or less, map<timepoint_t, deque<Market::AgentRecord>> in JSON form
	*/
    static void crow__list_agents(const crow::request&, crow::response&);

// list_agents

    /*
        POST /agent/get_history
        { "id": ... }

        Returns 
        {   "id": ...,
            "history": (map<timepoint_t, AgentAction> in JSON form)
        }
    */
    static void crow__get_agent_history(const crow::request&, crow::response&);

    /*
        POST /agent/del_history
        { "id": ... }

        Returns  TODO
        {   "id": ...,
        }
    */
    static void crow__delete_agent_history(const crow::request&, crow::response&);

    /*
        POST /info/emit
        [
            {   "type": (string representation of value of type enum Info::Types),
                "data": (JSON to be converted to an Info subclass, not including the `type` member)
            }
        ]
    */
    static void crow__emit_info(const crow::request&, crow::response&);

    /* 
        POST /subscribers/add
        [
            { "config": { ... }, "parameter": ... }
        ]

        Returns list<pair<int, variant<int, string> >> in JSON form:
            a mapping in list form from the integer index of a config object in the request
            to the resulting Subscriber::id_t (or the error produced)
        
    */
    static void crow__add_subscribers(const crow::request&, crow::response&);

    /*
        POST /subscribers/delete
        [ (list of IDs to delete) ]

        Returns
        [
            (true or string explaining error, for each provided ID)
        ]
    */
    static void crow__del_subscribers(const crow::request&, crow::response&);

    /*
        POST /subscribers/list
    */
    static void crow__list_subscribers(const crow::request&, crow::response&);

    static void crow__show_market_perf_data(const crow::request&, crow::response&);
    static void crow__reset_market_perf_data(const crow::request&, crow::response&);

    public:
    static std::shared_ptr<Interface> get_instance(std::shared_ptr<Market::Market>);

    json build_json(
        std::optional<enum InterfaceErrorCode> error_code, 
        std::string msg, 
        std::optional<json> data,
        std::optional<InterfaceResponseType> data_type
    );

    crow::response build_json_crow(
        std::optional<enum InterfaceErrorCode> error_code,
        std::string msg, 
        std::optional<json> data, 
        std::optional<InterfaceResponseType> data_type = InterfaceResponseType::Data,
        std::optional<int> http_code = std::nullopt
    );

    bool start();

    constexpr static const float api_version = 0.1000;
};
