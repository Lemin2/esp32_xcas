// Stub definitions for the Groebner-basis entry points exported by cocoa.cc,
// which was removed from the build (console-only port, size reduction).
// solve.cc still calls these for the gbasis/greduce commands; the stubs
// return false/empty so those commands fail gracefully instead of crashing.
#include "giacPCH.h"
using namespace std;
#include "cocoa.h"

#ifndef NO_NAMESPACE_GIAC
namespace giac {
#endif // ndef NO_NAMESPACE_GIAC

  bool f5(vectpoly & v,const gen & order){ return false; }

  bool cocoa_gbasis(vectpoly & v,const gen & order){ return false; }

  vecteur cocoa_in_ideal(const vectpoly & r,const vectpoly & v,const gen & order){ return vecteur(0); }

  bool cocoa_greduce(const vectpoly & r,const vectpoly & v,const gen & order,vectpoly & res){ return false; }

#if !defined CAS38_DISABLED && !defined FXCG && !defined KHICAS
  bool gbasis8(const vectpoly & v,order_t & order,vectpoly & res,environment * env,bool modularalgo,bool modularcheck,int & rur,GIAC_CONTEXT,gbasis_param_t gbasis_param,vector<vectpoly> * coeffsmodptr){ return false; }

  bool greduce8(const vectpoly & v,const vectpoly & G,order_t & order,vectpoly & res,environment * env,GIAC_CONTEXT){ return false; }
#endif

  longlong memory_usage(){ return 0; }

#ifndef NO_NAMESPACE_GIAC
} // namespace giac
#endif // ndef NO_NAMESPACE_GIAC
