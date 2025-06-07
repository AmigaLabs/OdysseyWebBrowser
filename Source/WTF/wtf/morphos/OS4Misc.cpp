#include "config.h"
#include <exec/types.h>
#include <stdlib.h>
#include <proto/exec.h>
#include "Platform.h"

namespace WTF {
class HasAltivec {
	private:	
		bool m_hasAltivec;

	public:	

	HasAltivec()
	{
		LONG altivec = 0;
		GetCPUInfoTags(GCIT_VectorUnit, &altivec, TAG_DONE);
		m_hasAltivec = altivec == 1;
	}

	bool hasAltivec()
	{
		static HasAltivec __hs;
		return m_hasAltivec;
	}
};
}
