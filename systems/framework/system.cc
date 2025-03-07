#include "drake/systems/framework/system.h"

#include <set>
#include <string_view>
#include <vector>

#include <fmt/format.h>

#include "drake/common/drake_deprecated.h"
#include "drake/common/unused.h"
#include "drake/systems/framework/system_visitor.h"

namespace drake {
namespace systems {

template <typename T>
System<T>::~System() {}

template <typename T>
void System<T>::Accept(SystemVisitor<T>* v) const {
  DRAKE_DEMAND(v != nullptr);
  v->VisitSystem(*this);
}

template <typename T>
std::unique_ptr<System<T>> System<T>::Clone() const {
  std::unique_ptr<System<T>> result;

  // To implement cloning, we'll scalar convert back and forth through some
  // intermediate scalar type "U". When T=double, we'll use U=AutoDiffXd since
  // its the scalar type most likely to exist. When T={AutoDiffXd,Expression}
  // we'll use U=double.
  using U = std::conditional_t<std::is_same_v<T, double>, AutoDiffXd, double>;

  // Convert T -> U -> T, stopping as soon as we hit a nullptr.
  auto intermediate = this->template ToScalarTypeMaybe<U>();
  if (intermediate != nullptr) {
    result = intermediate->template ToScalarTypeMaybe<T>();
  }

  // If anything went wrong, throw an exception.
  if (result == nullptr) {
    throw std::logic_error(
        fmt::format("System::Clone(): {} system '{}' does not support Cloning",
                    this->GetSystemType(), this->GetSystemPathname()));
  }

  return result;
}

template <typename T>
std::unique_ptr<Context<T>> System<T>::AllocateContext() const {
  return dynamic_pointer_cast_or_throw<Context<T>>(
      SystemBase::AllocateContext());
}

template <typename T>
std::unique_ptr<CompositeEventCollection<T>>
System<T>::AllocateCompositeEventCollection() const {
  auto result = DoAllocateCompositeEventCollection();
  result->set_system_id(get_system_id());
  return result;
}

template <typename T>
std::unique_ptr<BasicVector<T>> System<T>::AllocateInputVector(
    const InputPort<T>& input_port) const {
  DRAKE_THROW_UNLESS(input_port.get_data_type() == kVectorValued);
  const InputPortBase& self_input_port_base = this->GetInputPortBaseOrThrow(
      __func__, input_port.get_index(), /* warn_deprecated = */ false);
  DRAKE_THROW_UNLESS(&input_port == &self_input_port_base);
  std::unique_ptr<AbstractValue> value = DoAllocateInput(input_port);
  return value->get_value<BasicVector<T>>().Clone();
}

template <typename T>
std::unique_ptr<AbstractValue> System<T>::AllocateInputAbstract(
    const InputPort<T>& input_port) const {
  const int index = input_port.get_index();
  DRAKE_ASSERT(index >= 0 && index < num_input_ports());
  return DoAllocateInput(input_port);
}

template <typename T>
std::unique_ptr<SystemOutput<T>> System<T>::AllocateOutput() const {
  // make_unique can't invoke this private constructor.
  auto output = std::unique_ptr<SystemOutput<T>>(new SystemOutput<T>());
  for (int i = 0; i < this->num_output_ports(); ++i) {
    const auto& output_port = dynamic_cast<const OutputPort<T>&>(
        this->GetOutputPortBaseOrThrow(__func__, i,
                                       /* warn_deprecated = */ false));
    output->add_port(output_port.Allocate());
  }
  output->set_system_id(get_system_id());
  return output;
}

template <typename T>
std::unique_ptr<Context<T>> System<T>::CreateDefaultContext() const {
  std::unique_ptr<Context<T>> context = AllocateContext();
  SetDefaultContext(context.get());
  return context;
}

template <typename T>
void System<T>::SetDefaultContext(Context<T>* context) const {
  this->ValidateContext(context);

  // Set the default state, checking that the number of state variables does
  // not change.
  const int n_xc = context->num_continuous_states();
  const int n_xd = context->num_discrete_state_groups();
  const int n_xa = context->num_abstract_states();

  SetDefaultState(*context, &context->get_mutable_state());

  DRAKE_DEMAND(n_xc == context->num_continuous_states());
  DRAKE_DEMAND(n_xd == context->num_discrete_state_groups());
  DRAKE_DEMAND(n_xa == context->num_abstract_states());

  // Set the default parameters, checking that the number of parameters does
  // not change.
  const int num_params = context->num_numeric_parameter_groups();
  SetDefaultParameters(*context, &context->get_mutable_parameters());
  DRAKE_DEMAND(num_params == context->num_numeric_parameter_groups());
}

template <typename T>
void System<T>::SetRandomState(const Context<T>& context, State<T>* state,
                               RandomGenerator* generator) const {
  unused(generator);
  SetDefaultState(context, state);
}

template <typename T>
void System<T>::SetRandomParameters(const Context<T>& context,
                                    Parameters<T>* parameters,
                                    RandomGenerator* generator) const {
  unused(generator);
  SetDefaultParameters(context, parameters);
}

template <typename T>
void System<T>::SetRandomContext(Context<T>* context,
                                 RandomGenerator* generator) const {
  ValidateContext(context);

  // Set the default state, checking that the number of state variables does
  // not change.
  const int n_xc = context->num_continuous_states();
  const int n_xd = context->num_discrete_state_groups();
  const int n_xa = context->num_abstract_states();

  SetRandomState(*context, &context->get_mutable_state(), generator);

  DRAKE_DEMAND(n_xc == context->num_continuous_states());
  DRAKE_DEMAND(n_xd == context->num_discrete_state_groups());
  DRAKE_DEMAND(n_xa == context->num_abstract_states());

  // Set the default parameters, checking that the number of parameters does
  // not change.
  const int num_params = context->num_numeric_parameter_groups();
  SetRandomParameters(*context, &context->get_mutable_parameters(),
                      generator);
  DRAKE_DEMAND(num_params == context->num_numeric_parameter_groups());
}

template <typename T>
void System<T>::AllocateFixedInputs(Context<T>* context) const {
  ValidateContext(context);

  for (InputPortIndex i(0); i < num_input_ports(); ++i) {
    const InputPortBase& input_port_base = this->GetInputPortBaseOrThrow(
        __func__, i, /* warn_deprecated = */ false);
    const auto& port = dynamic_cast<const InputPort<T>&>(input_port_base);
    if (port.get_data_type() == kVectorValued) {
      port.FixValue(context, *AllocateInputVector(port));
    } else {
      DRAKE_DEMAND(port.get_data_type() == kAbstractValued);
      port.FixValue(context, *AllocateInputAbstract(port));
    }
  }
}

template <typename T>
bool System<T>::HasAnyDirectFeedthrough() const {
  return GetDirectFeedthroughs().size() > 0;
}

template <typename T>
bool System<T>::HasDirectFeedthrough(int output_port) const {
  std::multimap<int, int> pairs = GetDirectFeedthroughs();
  for (const auto& pair : pairs) {
    if (pair.second == output_port) return true;
  }
  return false;
}

template <typename T>
bool System<T>::HasDirectFeedthrough(int input_port, int output_port) const {
  std::multimap<int, int> pairs = GetDirectFeedthroughs();
  auto range = pairs.equal_range(input_port);
  for (auto i = range.first; i != range.second; ++i) {
    if (i->second == output_port) return true;
  }
  return false;
}

template <typename T>
EventStatus System<T>::Publish(const Context<T>& context,
                        const EventCollection<PublishEvent<T>>& events) const {
  ValidateContext(context);
  return DispatchPublishHandler(context, events);
}

template <typename T>
void System<T>::ForcedPublish(const Context<T>& context) const {
  const EventStatus status =
      Publish(context, this->get_forced_publish_events());
  status.ThrowOnFailure(__func__);
}

template <typename T>
const T& System<T>::EvalPotentialEnergy(const Context<T>& context) const {
  ValidateContext(context);
  const CacheEntry& entry =
      this->get_cache_entry(potential_energy_cache_index_);
  return entry.Eval<T>(context);
}

template <typename T>
const T& System<T>::EvalKineticEnergy(const Context<T>& context) const {
  ValidateContext(context);
  const CacheEntry& entry =
      this->get_cache_entry(kinetic_energy_cache_index_);
  return entry.Eval<T>(context);
}

template <typename T>
const T& System<T>::EvalConservativePower(const Context<T>& context) const {
  ValidateContext(context);
  const CacheEntry& entry =
      this->get_cache_entry(conservative_power_cache_index_);
  return entry.Eval<T>(context);
}

template <typename T>
const T& System<T>::EvalNonConservativePower(const Context<T>& context) const {
  ValidateContext(context);
  const CacheEntry& entry =
      this->get_cache_entry(nonconservative_power_cache_index_);
  return entry.Eval<T>(context);
}

template <typename T>
SystemConstraintIndex System<T>::AddExternalConstraint(
    ExternalSystemConstraint constraint) {
  const auto& calc = constraint.get_calc<T>();
  if (calc) {
    constraints_.emplace_back(std::make_unique<SystemConstraint<T>>(
        this, calc, constraint.bounds(), constraint.description()));
  } else {
    constraints_.emplace_back(std::make_unique<SystemConstraint<T>>(
        this, fmt::format(
            "{} (disabled for this scalar type)",
            constraint.description())));
  }
  external_constraints_.emplace_back(std::move(constraint));
  return SystemConstraintIndex(constraints_.size() - 1);
}

template <typename T>
void System<T>::CalcTimeDerivatives(const Context<T>& context,
                                    ContinuousState<T>* derivatives) const {
  DRAKE_DEMAND(derivatives != nullptr);
  ValidateContext(context);
  ValidateCreatedForThisSystem(derivatives);
  DoCalcTimeDerivatives(context, derivatives);
}

template <typename T>
void System<T>::CalcImplicitTimeDerivativesResidual(
    const Context<T>& context, const ContinuousState<T>& proposed_derivatives,
    EigenPtr<VectorX<T>> residual) const {
  DRAKE_DEMAND(residual != nullptr);
  if (residual->size() != this->implicit_time_derivatives_residual_size()) {
    throw std::logic_error(fmt::format(
        "CalcImplicitTimeDerivativesResidual(): expected "
        "residual vector of size {} but got one of size {}.\n"
        "Use AllocateImplicitTimeDerivativesResidual() to "
        "obtain a vector of the correct size.",
        this->implicit_time_derivatives_residual_size(), residual->size()));
  }
  ValidateContext(context);
  ValidateCreatedForThisSystem(proposed_derivatives);
  DoCalcImplicitTimeDerivativesResidual(context, proposed_derivatives,
                                        residual);
}

template <typename T>
EventStatus System<T>::CalcDiscreteVariableUpdate(
    const Context<T>& context,
    const EventCollection<DiscreteUpdateEvent<T>>& events,
    DiscreteValues<T>* discrete_state) const {
  ValidateContext(context);
  ValidateCreatedForThisSystem(discrete_state);

  return DispatchDiscreteVariableUpdateHandler(context, events, discrete_state);
}

template <typename T>
void System<T>::ApplyDiscreteVariableUpdate(
    const EventCollection<DiscreteUpdateEvent<T>>& events,
    DiscreteValues<T>* discrete_state, Context<T>* context) const {
  ValidateContext(context);
  ValidateCreatedForThisSystem(discrete_state);
  DoApplyDiscreteVariableUpdate(events, discrete_state, context);
}

template <typename T>
void System<T>::CalcForcedDiscreteVariableUpdate(
    const Context<T>& context,
    DiscreteValues<T>* discrete_state) const {
  const EventStatus status = CalcDiscreteVariableUpdate(
      context, this->get_forced_discrete_update_events(), discrete_state);
  status.ThrowOnFailure(__func__);
}

template <typename T>
EventStatus System<T>::CalcUnrestrictedUpdate(
    const Context<T>& context,
    const EventCollection<UnrestrictedUpdateEvent<T>>& events,
    State<T>* state) const {
  ValidateContext(context);
  ValidateCreatedForThisSystem(state);
  const int continuous_state_dim = state->get_continuous_state().size();
  const int discrete_state_dim = state->get_discrete_state().num_groups();
  const int abstract_state_dim = state->get_abstract_state().size();

  const EventStatus status =
      DispatchUnrestrictedUpdateHandler(context, events, state);

  if (continuous_state_dim != state->get_continuous_state().size() ||
      discrete_state_dim != state->get_discrete_state().num_groups() ||
      abstract_state_dim != state->get_abstract_state().size()) {
    throw std::logic_error(
        "State variable dimensions cannot be changed "
        "in CalcUnrestrictedUpdate().");
  }

  return status;
}

template <typename T>
void System<T>::ApplyUnrestrictedUpdate(
    const EventCollection<UnrestrictedUpdateEvent<T>>& events,
    State<T>* state, Context<T>* context) const {
  ValidateContext(context);
  ValidateCreatedForThisSystem(state);
  DoApplyUnrestrictedUpdate(events, state, context);
}

template <typename T>
void System<T>::CalcForcedUnrestrictedUpdate(const Context<T>& context,
                                             State<T>* state) const {
  const EventStatus status = CalcUnrestrictedUpdate(
      context, this->get_forced_unrestricted_update_events(), state);
  status.ThrowOnFailure(__func__);
}

template <typename T>
T System<T>::CalcNextUpdateTime(const Context<T>& context,
                                CompositeEventCollection<T>* events) const {
  ValidateContext(context);
  ValidateCreatedForThisSystem(events);
  DRAKE_DEMAND(events != nullptr);
  events->Clear();
  T time{NAN};
  DoCalcNextUpdateTime(context, events, &time);
  using std::isnan, std::isfinite;

  if (isnan(time)) {
    throw std::logic_error(
        fmt::format("System::CalcNextUpdateTime(): {} system '{}' overrode "
                    "DoCalcNextUpdateTime() but at time={} it returned with no "
                    "update time set (or the update time was set to NaN). "
                    "Return infinity to indicate no next update time.",
                    this->GetSystemType(), this->GetSystemPathname(),
                    ExtractDoubleOrThrow(context.get_time())));
  }

  if (isfinite(time) && !events->HasEvents()) {
    throw std::logic_error(fmt::format(
        "System::CalcNextUpdateTime(): {} system '{}' overrode "
        "DoCalcNextUpdateTime() but at time={} it returned update "
        "time {} with an empty Event collection. Return infinity "
        "to indicate no next update time; otherwise at least one "
        "Event object must be provided even if it does nothing.",
        this->GetSystemType(), this->GetSystemPathname(),
        ExtractDoubleOrThrow(context.get_time()), ExtractDoubleOrThrow(time)));
  }

  // If the context contains a perturbed current time, and
  // DoCalcNextUpdateTime() returned "right now" (which would be the
  // perturbed time here), we need to adjust the returned time to the actual
  // time. (Simulator::Initialize() perturbs time in that way.)
  if (context.get_true_time() && time == context.get_time())
    time = *context.get_true_time();

  return time;
}

template <typename T>
const DiscreteValues<T>& System<T>::EvalUniquePeriodicDiscreteUpdate(
    const Context<T>& context) const {
  const CacheEntry& entry =
      this->get_cache_entry(unique_periodic_discrete_update_cache_index_);
  return entry.Eval<DiscreteValues<T>>(context);
}

// Unit testing for this method is in diagram_test.cc where it is used as
// the leaf computation for the Diagram recursion.
template <typename T>
void System<T>::CalcUniquePeriodicDiscreteUpdate(
    const Context<T>& context, DiscreteValues<T>* discrete_values) const {
  ValidateContext(context);
  ValidateCreatedForThisSystem(discrete_values);

  // TODO(sherm1) We only need the DiscreteUpdateEvent portion of the
  //  CompositeEventCollection but don't have a convenient way to allocate
  //  that in a Leaf vs. Diagram agnostic way. Add that if needed for speed.
  auto collection = AllocateCompositeEventCollection();

  std::optional<PeriodicEventData> timing;
  FindUniquePeriodicDiscreteUpdatesOrThrow(
      __func__, *this, context, &timing,
      &collection->get_mutable_discrete_update_events());
  if (!timing.has_value()) {
    throw std::logic_error(
        fmt::format("{}(): there are no periodic discrete "
                    "update events in this System.", __func__));
  }

  // This should come up with the same result although calculated independently.
  // Too expensive to check in Release, but Debug is leisurely.
  DRAKE_ASSERT(*timing == *GetUniquePeriodicDiscreteUpdateAttribute());

  // Start with scratch discrete variables equal to the current values.
  discrete_values->SetFrom(context.get_discrete_state());

  // Then let the event handlers modify them or not.
  const EventStatus status = this->CalcDiscreteVariableUpdate(
      context, collection->get_discrete_update_events(), discrete_values);
  status.ThrowOnFailure(__func__);
}

template <typename T>
void System<T>::GetPeriodicEvents(const Context<T>& context,
                                  CompositeEventCollection<T>* events) const {
  ValidateContext(context);
  ValidateCreatedForThisSystem(events);
  events->Clear();
  DoGetPeriodicEvents(context, events);
}

template <typename T>
void System<T>::GetPerStepEvents(const Context<T>& context,
                                 CompositeEventCollection<T>* events) const {
  ValidateContext(context);
  ValidateCreatedForThisSystem(events);
  events->Clear();
  DoGetPerStepEvents(context, events);
}

template <typename T>
void System<T>::GetInitializationEvents(
    const Context<T>& context,
    CompositeEventCollection<T>* events) const {
  ValidateContext(context);
  ValidateCreatedForThisSystem(events);
  events->Clear();
  DoGetInitializationEvents(context, events);
}

template <typename T>
void System<T>::ExecuteInitializationEvents(Context<T>* context) const {
  auto discrete_updates = AllocateDiscreteVariables();
  auto state = context->CloneState();
  auto init_events = AllocateCompositeEventCollection();

  // NOTE: The execution order here must match the code in
  // Simulator::Initialize().
  GetInitializationEvents(*context, init_events.get());

  // Do unrestricted updates first.
  if (init_events->get_unrestricted_update_events().HasEvents()) {
    const EventStatus status = CalcUnrestrictedUpdate(
        *context, init_events->get_unrestricted_update_events(), state.get());
    status.ThrowOnFailure(__func__);
    ApplyUnrestrictedUpdate(init_events->get_unrestricted_update_events(),
                            state.get(), context);
  }
  // Do restricted (discrete variable) updates next.
  if (init_events->get_discrete_update_events().HasEvents()) {
    const EventStatus status = CalcDiscreteVariableUpdate(
        *context, init_events->get_discrete_update_events(),
        discrete_updates.get());
    status.ThrowOnFailure(__func__);
    ApplyDiscreteVariableUpdate(init_events->get_discrete_update_events(),
                                discrete_updates.get(), context);
  }
  // Do any publishes last.
  if (init_events->get_publish_events().HasEvents()) {
    const EventStatus status =
        Publish(*context, init_events->get_publish_events());
    status.ThrowOnFailure(__func__);
  }
}

template <typename T>
std::optional<PeriodicEventData>
    System<T>::GetUniquePeriodicDiscreteUpdateAttribute() const {
  std::optional<PeriodicEventData> saved_attr;
  auto periodic_events_map = MapPeriodicEventsByTiming();
  for (const auto& saved_attr_and_vector : periodic_events_map) {
    for (const auto& event : saved_attr_and_vector.second) {
      if (event->is_discrete_update()) {
        if (saved_attr)
          return std::nullopt;
        saved_attr = saved_attr_and_vector.first;
        break;
      }
    }
  }

  return saved_attr;
}

template <typename T>
bool System<T>::IsDifferenceEquationSystem(double* time_period) const {
  if (num_continuous_states() || num_abstract_states()) { return false; }

  if (num_discrete_state_groups() != 1) {
    return false;
  }

  std::optional<PeriodicEventData> periodic_data =
      GetUniquePeriodicDiscreteUpdateAttribute();
  if (!periodic_data) { return false; }
  if (periodic_data->offset_sec() != 0.0) { return false; }

  if (time_period != nullptr) {
    *time_period = periodic_data->period_sec();
  }
  return true;
}

template <typename T>
std::map<PeriodicEventData, std::vector<const Event<T>*>,
         PeriodicEventDataComparator>
System<T>::MapPeriodicEventsByTiming(const Context<T>* context) const {
  std::unique_ptr<Context<T>> dummy_context;
  const Context<T>* context_to_use = context;
  if (context_to_use == nullptr) {
    dummy_context = AllocateContext();
    context_to_use = dummy_context.get();
  }
  return DoMapPeriodicEventsByTiming(*context_to_use);
}

template <typename T>
void System<T>::CalcOutput(const Context<T>& context,
                           SystemOutput<T>* outputs) const {
  DRAKE_DEMAND(outputs != nullptr);
  ValidateContext(context);
  ValidateCreatedForThisSystem(outputs);
  for (OutputPortIndex i(0); i < num_output_ports(); ++i) {
    const auto& output_port = dynamic_cast<const OutputPort<T>&>(
        this->GetOutputPortBaseOrThrow(__func__, i,
                                       /* warn_deprecated = */ false));
    // TODO(sherm1) Would be better to use Eval() here but we don't have
    // a generic abstract assignment capability that would allow us to
    // copy into existing memory in `outputs` (rather than clone). User
    // code depends on memory stability in SystemOutput.
    output_port.Calc(context, outputs->GetMutableData(i));
  }
}

template <typename T>
T System<T>::CalcPotentialEnergy(const Context<T>& context) const {
  ValidateContext(context);
  return DoCalcPotentialEnergy(context);
}

template <typename T>
T System<T>::CalcKineticEnergy(const Context<T>& context) const {
  ValidateContext(context);
  return DoCalcKineticEnergy(context);
}

template <typename T>
T System<T>::CalcConservativePower(const Context<T>& context) const {
  ValidateContext(context);
  return DoCalcConservativePower(context);
}

template <typename T>
T System<T>::CalcNonConservativePower(const Context<T>& context) const {
  ValidateContext(context);
  return DoCalcNonConservativePower(context);
}

template <typename T>
void System<T>::MapVelocityToQDot(const Context<T>& context,
                                  const VectorBase<T>& generalized_velocity,
                                  VectorBase<T>* qdot) const {
  MapVelocityToQDot(context, generalized_velocity.CopyToVector(), qdot);
}

template <typename T>
void System<T>::MapVelocityToQDot(
    const Context<T>& context,
    const Eigen::Ref<const VectorX<T>>& generalized_velocity,
    VectorBase<T>* qdot) const {
  this->ValidateContext(context);
  DoMapVelocityToQDot(context, generalized_velocity, qdot);
}

template <typename T>
void System<T>::MapQDotToVelocity(const Context<T>& context,
                                  const VectorBase<T>& qdot,
                                  VectorBase<T>* generalized_velocity) const {
  MapQDotToVelocity(context, qdot.CopyToVector(), generalized_velocity);
}

template <typename T>
void System<T>::MapQDotToVelocity(const Context<T>& context,
                                  const Eigen::Ref<const VectorX<T>>& qdot,
                                  VectorBase<T>* generalized_velocity) const {
  this->ValidateContext(context);
  DoMapQDotToVelocity(context, qdot, generalized_velocity);
}

template <typename T>
const Context<T>& System<T>::GetSubsystemContext(
    const System<T>& subsystem,
    const Context<T>& context) const {
  ValidateContext(context);
  auto ret = DoGetTargetSystemContext(subsystem, &context);
  if (ret != nullptr) return *ret;

  throw std::logic_error(
      fmt::format("GetSubsystemContext(): {} subsystem '{}' is not "
                  "contained in {} System '{}'.",
                  subsystem.GetSystemType(), subsystem.GetSystemPathname(),
                  this->GetSystemType(), this->GetSystemPathname()));
}

template <typename T>
Context<T>& System<T>::GetMutableSubsystemContext(const System<T>& subsystem,
                                                  Context<T>* context) const {
  DRAKE_ASSERT(context != nullptr);
  // Make use of the const method to avoid code duplication.
  const Context<T>& subcontext = GetSubsystemContext(subsystem, *context);
  return const_cast<Context<T>&>(subcontext);
}

template <typename T>
const Context<T>& System<T>::GetMyContextFromRoot(
    const Context<T>& root_context) const {
  if (!root_context.is_root_context())
    throw std::logic_error(
        "GetMyContextFromRoot(): given context must be a root context.");
  const internal::SystemParentServiceInterface* parent_service =
      this->get_parent_service();
  if (!parent_service)  // This is the root System.
    return root_context;

  return static_cast<const System<T>&>(parent_service->GetRootSystemBase())
      .GetSubsystemContext(*this, root_context);
}

template <typename T>
Context<T>& System<T>::GetMyMutableContextFromRoot(
    Context<T>* root_context) const {
  DRAKE_DEMAND(root_context != nullptr);
  // Make use of the const method to avoid code duplication.
  const Context<T>& subcontext = GetMyContextFromRoot(*root_context);
  return const_cast<Context<T>&>(subcontext);
}

template <typename T>
const Context<T>* System<T>::DoGetTargetSystemContext(
    const System<T>& target_system, const Context<T>* context) const {
  if (&target_system == this) return context;
  return nullptr;
}

template <typename T>
State<T>* System<T>::DoGetMutableTargetSystemState(
    const System<T>& target_system, State<T>* state) const {
  if (&target_system == this) return state;
  return nullptr;
}

template <typename T>
const State<T>* System<T>::DoGetTargetSystemState(
    const System<T>& target_system, const State<T>* state) const {
  if (&target_system == this) return state;
  return nullptr;
}

template <typename T>
const ContinuousState<T>* System<T>::DoGetTargetSystemContinuousState(
    const System<T>& target_system,
    const ContinuousState<T>* xc) const {
  if (&target_system == this) return xc;
  return nullptr;
}

template <typename T>
CompositeEventCollection<T>*
System<T>::DoGetMutableTargetSystemCompositeEventCollection(
    const System<T>& target_system,
    CompositeEventCollection<T>* events) const {
  if (&target_system == this) return events;
  return nullptr;
}

template <typename T>
const CompositeEventCollection<T>*
System<T>::DoGetTargetSystemCompositeEventCollection(
    const System<T>& target_system,
    const CompositeEventCollection<T>* events) const {
  if (&target_system == this) return events;
  return nullptr;
}

template <typename T>
const InputPort<T>& System<T>::GetSoleInputPort() const {
  // Give a nice message if there were no inputs at all.
  if (num_input_ports() == 0) {
    throw std::logic_error(fmt::format(
        "System::get_input_port(): {} system '{}' does not have any inputs",
        this->GetSystemType(), this->GetSystemPathname()));
  }

  // Check if there is exactly one non-deprecated input port (and return it).
  int num_non_deprecated = 0;
  InputPortIndex non_deprecated_index;
  for (InputPortIndex i{0}; i < num_input_ports(); i++) {
    const InputPortBase& port_base = this->GetInputPortBaseOrThrow(
        __func__, i, /* warn_deprecated = */ false);
    if (port_base.get_deprecation() == std::nullopt) {
      ++num_non_deprecated;
      non_deprecated_index = i;
    }
  }
  if (num_non_deprecated == 1) {
    return get_input_port(non_deprecated_index);
  }

  // Too many inputs.
  throw std::logic_error(fmt::format(
      "System::get_input_port(): {} system '{}' has {} inputs, so this "
      "convenience function cannot be used; instead, use another overload "
      "e.g. get_input_port(InputPortIndex) or GetInputPort(string)",
      this->GetSystemType(), this->GetSystemPathname(), num_input_ports()));
}

template <typename T>
const InputPort<T>* System<T>::get_input_port_selection(
    std::variant<InputPortSelection, InputPortIndex> port_index) const {
  if (std::holds_alternative<InputPortIndex>(port_index)) {
    return &get_input_port(std::get<InputPortIndex>(port_index));
  }

  switch (std::get<InputPortSelection>(port_index)) {
    case InputPortSelection::kUseFirstInputIfItExists:
      if (num_input_ports() > 0) {
        return &get_input_port(0);
      }
      return nullptr;
    case InputPortSelection::kNoInput:
      return nullptr;
  }
  return nullptr;
}

template <typename T>
const InputPort<T>& System<T>::GetInputPort(
    const std::string& port_name) const {
  for (InputPortIndex i{0}; i < num_input_ports(); i++) {
    const InputPortBase& port_base = this->GetInputPortBaseOrThrow(
        __func__, i, /* warn_deprecated = */ false);
    if (port_name == port_base.get_name()) {
      return get_input_port(i);
    }
  }
  std::vector<std::string_view> port_names;
  port_names.reserve(num_input_ports());
  for (InputPortIndex i{0}; i < num_input_ports(); i++) {
    const InputPortBase& port_base = this->GetInputPortBaseOrThrow(
        __func__, i, /* warn_deprecated = */ false);
    port_names.push_back(port_base.get_name());
  }
  if (port_names.empty()) {
    port_names.push_back("it has no input ports");
  }
  throw std::logic_error(fmt::format(
      "System {} does not have an input port named {} "
      "(valid port names: {})",
      GetSystemName(), port_name, fmt::join(port_names, ", ")));
}

template <typename T>
bool System<T>::HasInputPort(
    const std::string& port_name) const {
  for (InputPortIndex i{0}; i < num_input_ports(); i++) {
    const InputPortBase& port_base = this->GetInputPortBaseOrThrow(
        __func__, i, /* warn_deprecated = */ false);
    if (port_name == port_base.get_name()) {
      // Call the getter (ignoring its return value), to allow deprecation
      // warnings to trigger.
      get_input_port(i);
      return true;
    }
  }
  return false;
}

template <typename T>
const OutputPort<T>& System<T>::GetSoleOutputPort() const {
  // Give a nice message if there were no outputs at all.
  if (num_output_ports() == 0) {
    throw std::logic_error(fmt::format(
        "System::get_output_port(): {} system '{}' does not have any outputs",
        this->GetSystemType(), this->GetSystemPathname()));
  }

  // Check if there is exactly one non-deprecated output port (and return it).
  int num_non_deprecated = 0;
  OutputPortIndex non_deprecated_index;
  for (OutputPortIndex i{0}; i < num_output_ports(); i++) {
    const OutputPortBase& port_base = this->GetOutputPortBaseOrThrow(
        __func__, i, /* warn_deprecated = */ false);
    if (port_base.get_deprecation() == std::nullopt) {
      ++num_non_deprecated;
      non_deprecated_index = i;
    }
  }
  if (num_non_deprecated == 1) {
    return get_output_port(non_deprecated_index);
  }

  // Too many outputs.
  throw std::logic_error(fmt::format(
      "System::get_output_port(): {} system '{}' has {} outputs, so this "
      "convenience function cannot be used; instead, use another overload "
      "e.g. get_output_port(OutputPortIndex) or GetOutputPort(string)",
      this->GetSystemType(), this->GetSystemPathname(), num_output_ports()));
}

template <typename T>
const OutputPort<T>* System<T>::get_output_port_selection(
    std::variant<OutputPortSelection, OutputPortIndex> port_index) const {
  if (std::holds_alternative<OutputPortIndex>(port_index)) {
    return &get_output_port(std::get<OutputPortIndex>(port_index));
  }
  switch (std::get<OutputPortSelection>(port_index)) {
    case OutputPortSelection::kUseFirstOutputIfItExists:
      if (num_output_ports() > 0) {
        return &get_output_port(0);
      }
      return nullptr;
    case OutputPortSelection::kNoOutput:
      return nullptr;
  }
  return nullptr;
}

template <typename T>
const OutputPort<T>& System<T>::GetOutputPort(
    const std::string& port_name) const {
  for (OutputPortIndex i{0}; i < num_output_ports(); i++) {
    const OutputPortBase& port_base = this->GetOutputPortBaseOrThrow(
        __func__, i, /* warn_deprecated = */ false);
    if (port_name == port_base.get_name()) {
      return get_output_port(i);
    }
  }
  std::vector<std::string_view> port_names;
  port_names.reserve(num_output_ports());
  for (OutputPortIndex i{0}; i < num_output_ports(); i++) {
    port_names.push_back(get_output_port_base(i).get_name());
  }
  if (port_names.empty()) {
    port_names.push_back("it has no output ports");
  }
  throw std::logic_error(fmt::format(
      "System {} does not have an output port named {} "
      "(valid port names: {})",
      GetSystemName(), port_name, fmt::join(port_names, ", ")));
}

template <typename T>
bool System<T>::HasOutputPort(
    const std::string& port_name) const {
  for (OutputPortIndex i{0}; i < num_output_ports(); i++) {
    const OutputPortBase& port_base = this->GetOutputPortBaseOrThrow(
        __func__, i, /* warn_deprecated = */ false);
    if (port_name == port_base.get_name()) {
      // Call the getter (ignoring its return value), to allow deprecation
      // warnings to trigger.
      get_output_port(i);
      return true;
    }
  }
  return false;
}

template <typename T>
int System<T>::num_constraints() const {
  return static_cast<int>(constraints_.size());
}

template <typename T>
const SystemConstraint<T>& System<T>::get_constraint(
    SystemConstraintIndex constraint_index) const {
  if (constraint_index < 0 || constraint_index >= num_constraints()) {
    throw std::out_of_range("System " + get_name() + ": Constraint index " +
                            std::to_string(constraint_index) +
                            " is out of range. There are only " +
                            std::to_string(num_constraints()) +
                            " constraints.");
  }
  return *constraints_[constraint_index];
}

template <typename T>
boolean<T> System<T>::CheckSystemConstraintsSatisfied(
    const Context<T>& context, double tol) const {
  ValidateContext(context);
  DRAKE_DEMAND(tol >= 0.0);
  boolean<T> result{true};
  for (const auto& constraint : constraints_) {
    result = result && constraint->CheckSatisfied(context, tol);
    // If T is a real number (not a symbolic expression), we can bail out
    // early with a diagnostic when the first constraint fails.
    if (scalar_predicate<T>::is_bool && !result) {
      DRAKE_LOGGER_DEBUG(
          "Context fails to satisfy SystemConstraint {}",
          constraint->description());
      return result;
    }
  }
  return result;
}

template <typename T>
VectorX<T> System<T>::CopyContinuousStateVector(
    const Context<T>& context) const {
  return context.get_continuous_state().CopyToVector();
}

template <typename T>
std::unique_ptr<System<AutoDiffXd>> System<T>::ToAutoDiffXd() const {
  return System<T>::ToAutoDiffXd(*this);
}

template <typename T>
std::unique_ptr<System<AutoDiffXd>> System<T>::ToAutoDiffXdMaybe() const {
  return ToScalarTypeMaybe<AutoDiffXd>();
}

template <typename T>
std::unique_ptr<System<symbolic::Expression>> System<T>::ToSymbolic() const {
  return System<T>::ToSymbolic(*this);
}

template <typename T>
std::unique_ptr<System<symbolic::Expression>>
System<T>::ToSymbolicMaybe() const {
  return ToScalarTypeMaybe<symbolic::Expression>();
}

template <typename T>
void System<T>::FixInputPortsFrom(const System<double>& other_system,
                                  const Context<double>& other_context,
                                  Context<T>* target_context) const {
  ValidateContext(target_context);
  other_system.ValidateContext(other_context);

  for (int i = 0; i < num_input_ports(); ++i) {
    const InputPortBase& input_port_base = this->GetInputPortBaseOrThrow(
        __func__, i, /* warn_deprecated = */ false);
    const InputPortBase& other_port_base = other_system.GetInputPortBaseOrThrow(
        __func__, i, /* warn_deprecated = */ false);
    const auto& input_port =
        dynamic_cast<const InputPort<T>&>(input_port_base);
    const auto& other_port =
        dynamic_cast<const InputPort<double>&>(other_port_base);
    if (!other_port.HasValue(other_context)) {
      continue;
    }

    switch (input_port.get_data_type()) {
      case kVectorValued: {
        // For vector-valued input ports, we placewise initialize a fixed
        // input vector using the explicit conversion from double to T.
        const VectorX<double>& other_vec = other_port.Eval(other_context);
        auto our_vec = this->AllocateInputVector(input_port);
        for (int j = 0; j < our_vec->size(); ++j) {
          (*our_vec)[j] = T(other_vec[j]);
        }
        input_port.FixValue(target_context, *our_vec);
        continue;
      }
      case kAbstractValued: {
        // For abstract-valued input ports, we just clone the value and fix
        // it to the port.
        const auto& other_value =
            other_port.Eval<AbstractValue>(other_context);
        input_port.FixValue(target_context, other_value);
        continue;
      }
    }
    DRAKE_UNREACHABLE();
  }
}

template <typename T>
const SystemScalarConverter& System<T>::get_system_scalar_converter() const {
  return system_scalar_converter_;
}

template <typename T>
void System<T>::GetWitnessFunctions(
    const Context<T>& context,
    std::vector<const WitnessFunction<T>*>* w) const {
  DRAKE_DEMAND(w != nullptr);
  DRAKE_DEMAND(w->empty());
  ValidateContext(context);
  DoGetWitnessFunctions(context, w);
}

template <typename T>
T System<T>::CalcWitnessValue(
    const Context<T>& context,
    const WitnessFunction<T>& witness_func) const {
  ValidateContext(context);
  return DoCalcWitnessValue(context, witness_func);
}

template <typename T>
void System<T>::DoGetWitnessFunctions(const Context<T>&,
    std::vector<const WitnessFunction<T>*>*) const {
}

template <typename T>
System<T>::System(SystemScalarConverter converter)
    : system_scalar_converter_(std::move(converter)) {
  // Note that configuration and kinematics tickets also include dependence
  // on parameters and accuracy, but not time or input ports.

  // Potential and kinetic energy, and conservative power that measures
  // the transfer between them, must _not_ be (explicitly) time dependent.
  // See API documentation above for Eval{Potential|Kinetic}Energy() and
  // EvalConservativePower() to see why.

  // TODO(sherm1) Due to issue #9171 we cannot always recognize which
  // variables contribute to configuration so we'll invalidate on all changes
  // except for time and inputs.  Once #9171 is resolved, we should use the
  // more specific configuration, kinematics, and mass tickets.
  const std::set<DependencyTicket> energy_prereqs_for_9171{
      accuracy_ticket(), all_state_ticket(), all_parameters_ticket()};
  potential_energy_cache_index_ =
      DeclareCacheEntry("potential energy",
          ValueProducer(this, &System<T>::CalcPotentialEnergy),
          energy_prereqs_for_9171)  // After #9171: configuration + mass.
          .cache_index();

  kinetic_energy_cache_index_ =
      DeclareCacheEntry("kinetic energy",
          ValueProducer(this, &System<T>::CalcKineticEnergy),
          energy_prereqs_for_9171)  // After #9171: kinematics + mass.
          .cache_index();

  conservative_power_cache_index_ =
      DeclareCacheEntry("conservative power",
          ValueProducer(this, &System<T>::CalcConservativePower),
          energy_prereqs_for_9171)  // After #9171: kinematics + mass.
          .cache_index();

  // Only non-conservative power can have an explicit time or input
  // port dependence. See API documentation above for
  // EvalNonConservativePower() to see why.
  nonconservative_power_cache_index_ =
      DeclareCacheEntry("non-conservative power",
          ValueProducer(this, &System<T>::CalcNonConservativePower),
          {all_sources_ticket()})  // This is correct.
          .cache_index();

  // We must assume that time derivatives can depend on *any* context source.
  time_derivatives_cache_index_ =
      this->DeclareCacheEntryWithKnownTicket(
              xcdot_ticket(), "time derivatives",
              ValueProducer(
                  this,
                  &System<T>::AllocateTimeDerivatives,
                  &System<T>::CalcTimeDerivatives),
              {all_sources_ticket()})
          .cache_index();

  // TODO(sherm1) Ideally a Diagram-level DiscreteValues cache object allocated
  //  here would reference its LeafSystem-level DiscreteValues cache objects
  //  rather than owning all these objects itself, and invoking
  //  EvalUniquePeriodicDiscreteUpdate() on the Diagram would update all the
  //  LeafSystem entries also. That would require a specialized version
  //  of AllocateDiscreteVariables() that would build the Diagram object from
  //  references to the already-allocated subsystem cache entries.
  unique_periodic_discrete_update_cache_index_ =
      this->DeclareCacheEntryWithKnownTicket(
              xd_unique_periodic_update_ticket(),
              "unique periodic discrete variable update",
              ValueProducer(this, &System<T>::AllocateDiscreteVariables,
                            &System<T>::CalcUniquePeriodicDiscreteUpdate),
              {all_sources_ticket()})
          .cache_index();
}

template <typename T>
InputPort<T>& System<T>::DeclareInputPort(
    std::variant<std::string, UseDefaultName> name, PortDataType type,
    int size, std::optional<RandomDistribution> random_type) {
  const InputPortIndex port_index(num_input_ports());

  const DependencyTicket port_ticket(this->assign_next_dependency_ticket());
  auto eval = [this, port_index](const ContextBase& context_base) {
    return this->EvalAbstractInput(context_base, port_index);
  };
  auto alloc = [this, port_index]() {
    return this->AllocateInputAbstract(this->get_input_port(port_index));
  };
  auto port = internal::FrameworkFactory::Make<InputPort<T>>(
      this, this, get_system_id(), NextInputPortName(std::move(name)),
      port_index, port_ticket, type, size, random_type, std::move(eval),
      std::move(alloc));
  InputPort<T>* port_ptr = port.get();
  this->AddInputPort(std::move(port));
  return *port_ptr;
}

template <typename T>
SystemConstraintIndex System<T>::AddConstraint(
    std::unique_ptr<SystemConstraint<T>> constraint) {
  DRAKE_DEMAND(constraint != nullptr);
  DRAKE_DEMAND(&constraint->get_system() == this);
  if (!external_constraints_.empty()) {
    throw std::logic_error(fmt::format(
        "System {} cannot add an internal constraint (named {}) "
        "after an external constraint (named {}) has already been added",
        GetSystemName(), constraint->description(),
        external_constraints_.front().description()));
  }
  constraint->set_system_id(this->get_system_id());
  constraints_.push_back(std::move(constraint));
  return SystemConstraintIndex(constraints_.size() - 1);
}

template <typename T>
void System<T>::DoCalcTimeDerivatives(const Context<T>& context,
                                      ContinuousState<T>* derivatives) const {
  // This default implementation is only valid for Systems with no continuous
  // state. Other Systems must override this method!
  unused(context);
  DRAKE_DEMAND(derivatives->size() == 0);
}

template <typename T>
void System<T>::DoCalcImplicitTimeDerivativesResidual(
      const Context<T>& context, const ContinuousState<T>& proposed_derivatives,
      EigenPtr<VectorX<T>> residual) const {
  // As documented, we can assume residual is non-null, its length matches the
  // declared size, and proposed_derivatives is compatible with this System.
  // However, this default implementation has an additional restriction: the
  // declared residual size must match the number of continuous states (that's
  // the default if no one says otherwise).
  if (residual->size() != proposed_derivatives.size()) {
    throw std::logic_error(fmt::format(
        "System::DoCalcImplicitTimeDerivativesResidual(): "
        "This default implementation requires that the declared residual size "
        "(here {}) matches the number of continuous state variables ({}). "
        "You must override this method if your residual is a different size.",
        residual->size(), proposed_derivatives.size()));
  }
  proposed_derivatives.get_vector().CopyToPreSizedVector(residual);
  *residual -= EvalTimeDerivatives(context).CopyToVector();
}

template <typename T>
void System<T>::DoCalcNextUpdateTime(const Context<T>& context,
                                     CompositeEventCollection<T>* events,
                                     T* time) const {
  unused(context, events);
  *time = std::numeric_limits<double>::infinity();
}

template <typename T>
void System<T>::DoGetPeriodicEvents(
    const Context<T>& context,
    CompositeEventCollection<T>* events) const {
  unused(context, events);
}

template <typename T>
void System<T>::DoGetPerStepEvents(
    const Context<T>& context,
    CompositeEventCollection<T>* events) const {
  unused(context, events);
}

template <typename T>
void System<T>::DoGetInitializationEvents(
    const Context<T>& context,
    CompositeEventCollection<T>* events) const {
  unused(context, events);
}

template <typename T>
T System<T>::DoCalcPotentialEnergy(const Context<T>& context) const {
  unused(context);
  return T(0);
}

template <typename T>
T System<T>::DoCalcKineticEnergy(const Context<T>& context) const {
  unused(context);
  return T(0);
}

template <typename T>
T System<T>::DoCalcConservativePower(const Context<T>& context) const {
  unused(context);
  return T(0);
}

template <typename T>
T System<T>::DoCalcNonConservativePower(const Context<T>& context) const {
  unused(context);
  return T(0);
}

template <typename T>
void System<T>::DoMapQDotToVelocity(const Context<T>& context,
                                    const Eigen::Ref<const VectorX<T>>& qdot,
                                    VectorBase<T>* generalized_velocity) const {
  unused(context);
  // In the particular case where generalized velocity and generalized
  // configuration are not even the same size, we detect this error and abort.
  // This check will thus not identify cases where the generalized velocity
  // and time derivative of generalized configuration are identically sized
  // but not identical!
  const int n = qdot.size();
  // You need to override System<T>::DoMapQDottoVelocity!
  DRAKE_THROW_UNLESS(generalized_velocity->size() == n);
  generalized_velocity->SetFromVector(qdot);
}

template <typename T>
void System<T>::DoMapVelocityToQDot(
    const Context<T>& context,
    const Eigen::Ref<const VectorX<T>>& generalized_velocity,
    VectorBase<T>* qdot) const {
  unused(context);
  // In the particular case where generalized velocity and generalized
  // configuration are not even the same size, we detect this error and abort.
  // This check will thus not identify cases where the generalized velocity
  // and time derivative of generalized configuration are identically sized
  // but not identical!
  const int n = generalized_velocity.size();
  // You need to override System<T>::DoMapVelocityToQDot!
  DRAKE_THROW_UNLESS(qdot->size() == n);
  qdot->SetFromVector(generalized_velocity);
}

template <typename T>
Eigen::VectorBlock<VectorX<T>> System<T>::GetMutableOutputVector(
    SystemOutput<T>* output, int port_index) const {
  DRAKE_ASSERT(0 <= port_index && port_index < num_output_ports());
  DRAKE_DEMAND(output != nullptr);
  ValidateCreatedForThisSystem(output);

  BasicVector<T>* output_vector = output->GetMutableVectorData(port_index);
  DRAKE_ASSERT(output_vector != nullptr);
  DRAKE_ASSERT(output_vector->size() == get_output_port(port_index).size());

  return output_vector->get_mutable_value();
}

template <typename T>
std::function<void(const AbstractValue&)>
System<T>::MakeFixInputPortTypeChecker(
    InputPortIndex port_index) const {
  const InputPortBase& port_base = this->GetInputPortBaseOrThrow(
      __func__, port_index, /* warn_deprecated = */ false);
  const InputPort<T>& port = static_cast<const InputPort<T>&>(port_base);
  const std::string& port_name = port.get_name();
  const std::string path_name = this->GetSystemPathname();

  // Note that our lambdas below will capture all necessary items by-value,
  // so that they do not rely on this System still being alive.  (We do not
  // allow a Context and System to have pointers to each other.)
  switch (port.get_data_type()) {
    case kAbstractValued: {
      // For abstract inputs, we only need to ensure that both runtime values
      // share the same base T in the Value<T>. Even if the System declared a
      // model_value that was a subtype of T, there is no EvalInputValue
      // sugar that allows the System to evaluate the input by downcasting to
      // that subtype, so here we should not insist that some dynamic_cast
      // would succeed. If the user writes the downcast on their own, it's
      // fine to let them also handle detailed error reporting on their own.
      const std::type_info& expected_type =
          this->AllocateInputAbstract(port)->static_type_info();
      return [&expected_type, port_index, path_name, port_name](
          const AbstractValue& actual) {
        if (actual.static_type_info() != expected_type) {
          SystemBase::ThrowInputPortHasWrongType(
              "FixInputPortTypeCheck", path_name, port_index, port_name,
              NiceTypeName::Get(expected_type),
              NiceTypeName::Get(actual.type_info()));
        }
      };
    }
    case kVectorValued: {
      // For vector inputs, check that the size is the same.
      // TODO(jwnimmer-tri) We should type-check the vector, eventually.
      const std::unique_ptr<BasicVector<T>> model_vector =
          this->AllocateInputVector(port);
      const int expected_size = model_vector->size();
      return [expected_size, port_index, path_name, port_name](
          const AbstractValue& actual) {
        const BasicVector<T>* const actual_vector =
            actual.maybe_get_value<BasicVector<T>>();
        if (actual_vector == nullptr) {
          SystemBase::ThrowInputPortHasWrongType(
              "FixInputPortTypeCheck", path_name, port_index, port_name,
              NiceTypeName::Get<Value<BasicVector<T>>>(),
              NiceTypeName::Get(actual));
        }
        // Check that vector sizes match.
        if (actual_vector->size() != expected_size) {
          SystemBase::ThrowInputPortHasWrongType(
              "FixInputPortTypeCheck", path_name, port_index, port_name,
              fmt::format("{} with size={}",
                          NiceTypeName::Get<BasicVector<T>>(),
                          expected_size),
              fmt::format("{} with size={}",
                          NiceTypeName::Get(*actual_vector),
                          actual_vector->size()));
        }
      };
    }
  }
  DRAKE_UNREACHABLE();
}

template <typename T>
const BasicVector<T>* System<T>::EvalBasicVectorInputImpl(
    const char* func, const Context<T>& context,
    InputPortIndex port_index) const {
  // Make sure this is the right kind of port before worrying about whether
  // it is connected up properly.
  const InputPortBase& port = GetInputPortBaseOrThrow(
      func, port_index, /* warn_deprecated = */ true);
  if (port.get_data_type() != kVectorValued)
    ThrowNotAVectorInputPort(func, port_index);

  // If there is no value at all, the port is not connected which is not
  // a problem here.
  const AbstractValue* const abstract_value =
      EvalAbstractInputImpl(func, context, port_index);
  if (abstract_value == nullptr) {
    return nullptr;
  }

  // We have a vector port with a value, it better be a BasicVector!
  const auto* basic_vector = &abstract_value->get_value<BasicVector<T>>();

  // Shouldn't have been possible to create this vector-valued port with
  // the wrong size.
  DRAKE_DEMAND(basic_vector->size() == port.size());

  return basic_vector;
}

template <typename T>
void System<T>::AddExternalConstraints(
    const std::vector<ExternalSystemConstraint>& constraints) {
  for (const auto& item : constraints) {
    AddExternalConstraint(item);
  }
}
}  // namespace systems
}  // namespace drake

DRAKE_DEFINE_CLASS_TEMPLATE_INSTANTIATIONS_ON_DEFAULT_SCALARS(
    class ::drake::systems::System)
