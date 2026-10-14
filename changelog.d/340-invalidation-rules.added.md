- **Every container states its iterator and reference invalidation rules
  (#340).** Each type checked by the conformance suite has an "Iterator
  invalidation" or "Reference invalidation" paragraph in its class
  documentation, saying which operations move or end the elements already
  held. The conformance suite checks that adding and removing behave as each
  paragraph says.
