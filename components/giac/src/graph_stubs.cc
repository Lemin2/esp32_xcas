// Stub definitions for graph-theory entry points still referenced by
// plot.cc / misc.cc / gen.cc after graphe.cc and graphtheory.cc were
// removed from the build (console-only port, graph theory disabled).
// is_graphe() always returns false, so the dead branches that call the
// other stubs are never actually taken at runtime.
#include "giacPCH.h"
using namespace std;
#include "gen.h"

#ifndef NO_NAMESPACE_GIAC
namespace giac {
#endif // ndef NO_NAMESPACE_GIAC

  bool is_graphe(const gen & g){ return false; }

  gen _graph_vertices(const gen & g,const context * contextptr){ return g; }

  gen _is_planar(const gen & g,const context * contextptr){ return g; }

  gen _graph_charpoly(const gen & g,const context * contextptr){ return g; }

#ifndef NO_NAMESPACE_GIAC
} // namespace giac
#endif // ndef NO_NAMESPACE_GIAC
