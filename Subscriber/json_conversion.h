#ifndef SUBSCRIBERJSON_H
#define SUBSCRIBERJSON_H 1

#include "common.h"
#include "Subscriber.h"
#include "Factory.h"


namespace Subscriber {

// json conversion 
//
void to_json(json& j, const record_type_t t);

void from_json(const json&, Config&);

void from_json(const json&, EndpointConfig&);
void to_json(json& j, const EndpointConfig c);

void from_json(const json&, FactoryParameter<AgentAction>&); 
void from_json(const json&, FactoryParameter<price_t>&); 


void to_json(json& j, const Subscribers::list_entry_t x);


//
//

};

#endif
