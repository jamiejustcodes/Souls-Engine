# Souls Engine artwork

`souls-engine.png` is the original 1024 x 687 PNG provided by the project
maintainer. Its bytes are preserved. CMake embeds the image into SoulsBrand;
stb_image decodes RGBA once at startup, without filesystem lookups during play.

About shows the full crest and wordmark. Compact UI and the SDL window icon use
the 320 x 320 crest region starting at pixel (352, 113). This is a presentation
crop, not a modified source asset. GraphicsUI uploads the RGBA texture once,
keeps it alive across viewport resizes, and releases it after GPU retirement.
