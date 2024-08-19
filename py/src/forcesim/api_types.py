from typing import List, Tuple, Any, Optional, Dict

from dataclasses import dataclass, asdict
from enum import Enum, auto

class enum_base(Enum):
    def __str__(self):
        return self.name

class Direction(enum_base):
    UP = auto()
    DOWN = auto()

class error_code_t(enum_base):
    Already_started = auto()
    General_error = auto()
    Json_parse_error = auto()
    Json_type_error = auto()
    Multiple = auto()
    Not_found = auto()
    Agent_not_implemented = auto()
    Agent_config_error = auto()
    Subscriber_config_error = auto()

class response_type_t(enum_base):
    Data = auto()
    Multiple_stringmap = auto()
    Multiple_pairlist = auto()
    Multiple_barelist = auto()

    def is_multiple(self):
        return self.value in [
            self.Multiple_barelist.value, 
            self.Multiple_pairlist.value, 
            self.Multiple_stringmap.value, 
        ]


class agentclass_t(enum_base):
    TrivialAgent = auto()
    ModeledCohortAgent_v1 = auto()
    BasicNormalDistAgent = auto()
    ModeledCohortAgent_v2 = auto()

class infotype_t(enum_base):
    Subjective = auto()

class subscriber_type_t(enum_base):
    AGENT_ACTION = auto()
    PRICE = auto()


def str_to_agentclass(s : str) -> agentclass_t:
    return agentclass_t.__getitem__(s)

def str_to_subscriber_type(s : str) -> subscriber_type_t:
    return subscriber_type_t.__getitem__(s)

def str_to_infotype(s : str) -> infotype_t:
    return infotype_t.__getitem__(s)

def str_to_error_code(s : str) -> error_code_t:
    return error_code_t.__getitem__(s)

def str_to_response_type(s : str) -> response_type_t:
    return response_type_t.__getitem__(s)


#@dataclass(kw_only=True)
@dataclass()
class AgentConfig():
    external_force: float
    schedule_every: int


@dataclass()
class AgentSpec():
    type : agentclass_t
    config : AgentConfig


    #count : int

#@dataclass(kw_only=True)
# a TrivialAgent always takes the same action, which is fully specified in 
# its config
@dataclass()
class TrivialAgentConfig(AgentConfig):
    direction: Direction
    internal_force: float

@dataclass()
class BasicNormalDistAgentConfig(AgentConfig):
    mean: float
    stddev: float
    

#@dataclass(kw_only=True)
@dataclass()
class ModeledCohortAgent_v1Config(AgentConfig):

	# the variance used to initialize the RV that the agent samples from
	# as Subjective Info objects are emitted, the agent updates the variance 
	# based on the subjectivity_extent of the info.
    initial_variance: float 

	# when info of type Subjective is received, its subjectivity_extent (float in
	# [-1,1]) is multiplied by the variance_multiplier to determine the variance
	# of the normal RV that the agent samples from 
    variance_multiplier: float

	# must be a positive number
	# the agent determines the amount of force in its action by comparing the current 
	# market price with its view of the correct price; the larger the difference, the
	# larger the force. 
	# since there is a global limit on the internal force that an agent can act with,
	# force_threshold is used to specify what price vs. view difference results in that
	# maximum force; any larger difference will not increase the force.
    force_threshold: float

    default_price_view: float


@dataclass
class ModeledCohortAgent_v2Config(ModeledCohortAgent_v1Config):
    distribution_parameters: List[float]




agentconf_ctor = {                                           
    agentclass_t.TrivialAgent: TrivialAgentConfig,           
    agentclass_t.BasicNormalDistAgent: BasicNormalDistAgentConfig,           
    agentclass_t.ModeledCohortAgent_v1: ModeledCohortAgent_v1Config,
    agentclass_t.ModeledCohortAgent_v2: ModeledCohortAgent_v2Config
}

#@dataclass(kw_only=True, frozen=True)
@dataclass(frozen=True)
class SubscriberConfig():
    type : subscriber_type_t
    port : int
    granularity : int = 1
    addr : str = "0.0.0.0"
    parameter: Any = None





@dataclass(frozen=True)
class Info():
    type : infotype_t 


@dataclass(frozen=True)
class SubjectiveInfo(Info):
	subjectivity_extent: float
    # TODO possibly use numpy.float128 - not worth adding the dependency for now
	price_indication: float
	is_relative: bool


info_ctor = {                                           
    infotype_t.Subjective: SubjectiveInfo,
}