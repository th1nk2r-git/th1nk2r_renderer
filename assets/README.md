# Third-party assets

The model, geometry, texture, and other media files in this directory are
third-party assets. They are not covered by the MIT License in the repository
root. Each asset remains subject to the rights and license granted by its
respective copyright holder.

## Sponza

**License status: pending verification.**

The model and material texture files under `models/sponza/` appear to originate
from the legacy Sponza package distributed through the Khronos glTF sample-model
collection. The Khronos
asset record identifies that package as © 2016 Crytek under the CRYENGINE
Limited License Agreement. These files must not be treated as MIT-licensed.

Before publishing a release or using the project commercially, replace these
files with an asset carrying a clearly compatible license, remove them from
the distribution, or obtain and document explicit redistribution permission
from the relevant rights holder.

Reference:

- <https://github.com/KhronosGroup/glTF-Sample-Assets/blob/main/Models/Sponza/README.md>

## Mud Road (Pure Sky) HDRI

`models/sponza/mud_road_puresky_2k.hdr` is used as the sample scene's
image-based lighting environment. Its filename identifies the
[Mud Road (Pure Sky) HDRI](https://polyhaven.com/a/mud_road_puresky) from
Poly Haven, with sky edits by Jarod Guest and the original by Sergey Rudavin.
Poly Haven distributes that asset under [CC0](https://polyhaven.com/license).

## Material preview models

`models/blocks/` contains GLB material preview models. The application imports
them at startup, but the default scene only creates a Sponza entity.

Each preview directory includes a `credits.txt` recording the source texture's
title, source URL and authors. Those records reference Poly Haven, ambientCG
and cgbookcase. Consult the credit file and the linked source for each asset's
license; this README does not establish a common license for the preview models.
