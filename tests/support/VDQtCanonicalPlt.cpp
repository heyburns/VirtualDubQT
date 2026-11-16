// Compiled without PIC, separately from Qt. This forces an ELF executable's
// canonical PLT address, reproducing valid Fedora builds that fooled dladdr().
extern "C" unsigned avcodec_version();
extern "C" const void* vdqtNonPicVersionAddress() {
    return reinterpret_cast<const void*>(&avcodec_version);
}
