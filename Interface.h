
#include <crow.h>
#include "Market.h"
#include "Agent/Factory.h"
#include "json_conversion.h"

#include <functional>
#include <optional>

#include <asio.hpp>


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


// translate from the RetKey type present in the return value of a list request helper
// to the possible return types indicated in the InterfaceResponseType enum
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
 * application of the endpoint's logic to each element (request) in that list, or to the entire list
 * and assembling the results into a list (or a map) for the response.
 * 
 * endpoint methods (Interface::crow__*) which support this call either list_generator_helper or 
 * list_handler_helper, which both call the main helper function, list_helper:
 * 
 *  template<typename InputItem, typename RetKey, typename RetVal, typename HandlerType>
 *  static void list_helper(const crow::request& req, crow::response& res, 
 *       HandlerType handler_f, 
 *       std::optional<std::function<void(int)>> finally_f = std::nullopt);
 * 
 * list_helper does the following:
 * - parse request JSON
 * - interpret the JSON as an array of items of type InputItem (see function signature above)
 * - run the function of type HandlerType on the input, which assembles them in a result
 *      structure of the appropriate type (depending on RetKey and RetValue)
 * - optionally run a "finally" function after all the input is processed
 * - convert the assembled results to JSON and build the response
 * 
 * list_helper uses an "adapter" (list_helper_adapter) to run the HandlerType handler, 
 *  which takes care of both running handler_f and also assembling the results in the result structure.
 * For each adapter, there is exactly one HandlerType type: the list_helper_adapter struct 
 *  template is specialized by HandlerType.
 *  
 * 
 * Currently, HandlerType (ie the type of callback provided by the endpoint method) can be one of:
 * - list_helper_generator_t (used by list_generator_helper)
 * - list_helper_handler_t (used by list_handler_helper)
 * 
 * list_generator_helper runs a function for each item in the input list, with the response list
 *  just being the list of return values from each invocation, and where the position of a response value
 *  in that list corresponds to the position of the request value in the request list. (Multiple_barelist)
 * list_handler_helper runs the function once on the entire list, and populates the response list
 *  in whatever way it needs to. (Multiple_pairlist or Multiple_stringmap)
 * 
 * 
 * See the comments for 
 * 
 * TODO possibly rename HandlerType to HelperType for more consistent terminiology
  ******************************************************** */

using list_error_t = std::tuple<InterfaceErrorCode, std::string>;

template<typename T>
using list_ret_t = std::variant<T, list_error_t>;

template<typename K, typename V>
using list_retmap_t = std::map<K, list_ret_t<V>>;


template<typename InputItem, typename RetKey, typename RetVal, typename HandlerType>
struct list_helper_adapter;

// TODO update this
// type of inner request handler which is executed (and returns a value) for each element 
// in the list provided in the request
// data structure returned to HTTP client is a Multiple_barelist, i.e.
//  a list where each value is returned from an individual invocation of the handler,
//  and where its position corresponds to the position in the request list
template<typename InputItem, typename RetVal>
using list_helper_generator_t = 
    std::function<list_ret_t<RetVal>(std::shared_ptr<Interface>, InputItem&)>;

// specialization for list_helper_generator_t
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
        It's easier for the handlers to be static, because passing them as handlers (ie inline) to 
        the CROW_ROUTE macro when they are non-static function pointers / methods does
        not appear to be possible with std::bind.
        Passing them as plain function pointers without std::bind and without a bound `this`
        works as expected. 
        Interface works well as a "singleton" class, so this design is not inconvenient.

        These functions must not be called until the Interface singleton is instantiated, since they
        access the instance pointer (Interface::instance)
    */

    // TODO documentation of the overall return structure and the format for
    // multiple return values 

    /*
        POST /market/run
        {
            "iter_count": (optional JSON integer value: number of iterations to run)
        }

        Return data:
            null
    */
    static void crow__market_run(const crow::request& req, crow::response&);
    /*
        POST /market/pause

        Return data:
            null
        
        Errors:
    */
    static void crow__market_pause(const crow::request&, crow::response&);
    /*
        POST /market/wait_for_pause
        {
            "timepoint": (optional JSON integer value of latest timepoint to wait before timing out)
        }

        Return data:
            {
                "timepoint": (JSON integer value of timepoint when pause took place)
            }

        Errors:
            General_error   if the request timed out
            {
                "limit":    (JSON integer value of timeout argument provided in the request)
            }

            General_error   if we unexpectedly returned earlier than the supplied timeout value
            null
        
    */
    static void crow__market_wait_for_pause(const crow::request&, crow::response&);

    /*
        POST /market/configure
            <Market::Config>

        Return data:
            null
    */
    static void crow__market_configure(const crow::request&, crow::response&);
    

    /*
        POST /market/start

        Return data:
            null
        
        Errors:
            Already_started
    */
    static void crow__market_start(const crow::request& req, crow::response& resp);


    /*
        POST /market/reset
        
        Return data:
            null
    */
    static void crow__market_reset(const crow::request& req, crow::response& resp);


    /*
        GET /market/price_history
        {
            "erase": (required: whether to erase the history after returning it)
        }

        Return data:
            <JSON object mapping timepoint values to floating-point price_t values>
        
        Errors:
            General_error
                (if the `erase` argument is missing)
    */
    static void crow__get_price_history(const crow::request&, crow::response&);

    /*
        POST /agent/add
        [
            {   "type": (value of type enum AgentType)
                "count": (integer: number of agents to create with this config)
                "config": (JSON form of an AgentConfig class (see Agent.h))
            }
        ]

        Return data: 
        Multiple_barelist
        [ 
            {
                "ids": [ 
                    (list of IDs created for this config (size = count parameter above))
                ]
            }
        ]

        Errors:
        Agent_config_error
            (missing value from the config or some other error during agent creation)
    */
    static void crow__add_agents(const crow::request&, crow::response&);


	/*
		POST /agent/delete
		[ id1, id2, ... ]

		Return data: 
        Multiple_pairlist   (numeric agent ID -> 
                                (bool) whether successfully deleted
                            )

        Errors:
        Not_found "agent not found"

	*/
    static void crow__del_agents(const crow::request&, crow::response&);



	/*
		GET /agent/list
		
        Return data: 
            map<timepoint_t, deque<Market::AgentRecord>> in JSON form
	*/
    static void crow__list_agents(const crow::request&, crow::response&);

    /*
        POST /agent/get_history
        { 
            "id": (integer agentid_t)
        }

        Returns data:
        {   "id": (integer agentid_t)
            "history": (map<timepoint_t, AgentAction> in JSON form)
        }
    */
    static void crow__get_agent_history(const crow::request&, crow::response&);

    /*
        TODO unimplemented

        POST /agent/del_history
        { 
            "id": (integer agentid_t)
        }
        

        Return data:
        {   
            "id": (integer agentid_t),
            "count": (integer number of entries deleted)
        }
    */
    static void crow__delete_agent_history(const crow::request&, crow::response&);

    /*
        POST /info/emit
        (list of Info objects to be included in an Infoset_t)
        [
            {   "type": (string representation of value of type enum Info::Types),
                "data": (JSON to be converted to an Info subclass, not including the `type` member)
            }
        ]

        Return data:
        {
            "timepoint": (integer timepoint when the Infoset_t was emitted)
        }

        Errors:
        Json_parse_error (if Info objects couldn't be parsed)
        General_error (if Market::emit_info encountered an error)
        
    */
    static void crow__emit_info(const crow::request&, crow::response&);

    /* 
        POST /subscribers/add
        [
            { 
                "config": { ... }, 
                "parameter": ... 
            }
        ]

        Return data:
        Multiple_barelist
        [
            (integer Subscriber::Id_t in JSON form)
        ]

        Errors:
        Subscriber_config_error
            if a JSON error occurred or the provided configuration is invalid
        General_error
            error encountered while adding the Subscriber with Subscribers::add    
        
    */
    static void crow__add_subscribers(const crow::request&, crow::response&);

    /*
        POST /subscribers/delete
        [ 
            (integer subscriber IDs to delete)
        ]

        Return data:
        Multiple_pairlist
        [
            [   (integer Subscriber::id_t),
                (true or string explaining error)
            ]
        ]
    */
    static void crow__del_subscribers(const crow::request&, crow::response&);

    /*
        GET /subscribers/list

        Return data:
        [
            Subscribers::list_entry_t in JSON form =
            { 
                id: Subscribers::id_t,
                pending_records: integer
                endpoint: string
                record_type: string form of Subscribers::record_type_t enum
            }
        ]

    */
    static void crow__list_subscribers(const crow::request&, crow::response&);


    /*
        GET /market/showperf

        map<string, map<timepoint_t, uintmax_t>> in JSON form
        perf key -> map from timepoint of collection to value in ms
    */
    static void crow__show_market_perf_data(const crow::request&, crow::response&);

    /*
        POST /market/resetperf
    */
    static void crow__reset_market_perf_data(const crow::request&, crow::response&);

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

    public:
    static std::shared_ptr<Interface> get_instance(std::shared_ptr<Market::Market>);
    static std::shared_ptr<Interface> get_instance();


    bool start(std::optional<asio::ip::address>, int);
    void stop();

    constexpr static const float api_version = 0.1000;
};
