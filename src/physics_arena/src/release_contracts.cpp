#include "physics_arena/release_contracts.h"

#include <string_view>

namespace physics_arena
{
std::string_view ReleaseTextView(const ReleaseCatalog* releaseCatalog, CatalogText text)
{
	if (releaseCatalog == nullptr || text.offset > releaseCatalog->textArenaUsed ||
	    text.size > releaseCatalog->textArenaUsed - text.offset)
		return {};
	return std::string_view(releaseCatalog->textArena.data() + text.offset, text.size);
}

std::string_view ResultTextView(const ResultManifestRecord* resultManifest, CatalogText text)
{
	if (resultManifest == nullptr || text.offset > resultManifest->textArenaUsed ||
	    text.size > resultManifest->textArenaUsed - text.offset)
		return {};
	return std::string_view(resultManifest->textArena.data() + text.offset, text.size);
}
}
