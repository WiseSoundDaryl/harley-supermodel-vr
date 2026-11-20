#ifndef INCLUDED_CINEMATICTOGGLE_H
#define INCLUDED_CINEMATICTOGGLE_H

// Pure key-edge state for the optional desktop UI preview. The caller owns
// polling and renderer configuration; this helper never accesses either.
class CinematicToggleState
{
public:
  explicit CinematicToggleState(bool initialCinematic = false)
    : m_cinematic(initialCinematic) {}

  bool Enabled() const { return m_cinematic; }

  // Return true only when a fresh focused press changes presentation mode.
  // A held startup key or focus return cannot toggle. The caller enables this
  // helper only for the optional mouse-look UI preview.
  bool Update(bool focused, bool keyDown)
  {
    if (!focused)
    {
      m_requireRelease = true;
      return false;
    }
    if (!keyDown)
    {
      m_requireRelease = false;
      return false;
    }
    if (m_requireRelease)
      return false;

    m_requireRelease = true;
    m_cinematic = !m_cinematic;
    return true;
  }

private:
  bool m_cinematic;
  bool m_requireRelease = true;
};

#endif // INCLUDED_CINEMATICTOGGLE_H
