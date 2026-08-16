# Scope

## In Scope

- large-mode formalization for the existing AIMD chunk-upload implementation
- evidence-backed verification of the current scheduler and backend safety path
- durable performance guidance for future pressure testing
- repository runtime state updates under `.ai/`

## Out of Scope

- redesigning the existing AIMD algorithm
- changing chunk API contracts
- fabricating benchmark numbers without running real pressure tests
- adding a new benchmark harness that installs dependencies automatically

## Key Risks

- the current workstation does not expose `docker`, so backend container build
  and full-stack pressure testing are not runnable here
- existing repository-wide frontend test issues outside the AIMD suite can fail
  a full `npm test`
