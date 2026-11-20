#ifndef INCLUDED_HARLEY_CABINET_INPUT_H
#define INCLUDED_HARLEY_CABINET_INPUT_H

// Consume already-latched native-frame edges, never held display-frame input.
// The cabinet uses Service to cycle and Test to confirm; its EXIT item returns
// from a page. There is no universal cabinet Back input.
namespace HarleyCabinetInput
{
struct Buttons { bool coin, start, service, test; };
inline Buttons Map(bool arcade, bool coinEdge, bool selectEdge)
{
  return {!arcade && coinEdge, !arcade && selectEdge,
          arcade && coinEdge, arcade && selectEdge};
}
}
#endif
