package mosaic.vanilla;

/** Provider 选择:按 mcVersion。 */
public final class MosaicProviderRegistry {
    private static final java.util.List<MosaicProvider> PROVIDERS = new java.util.ArrayList<>();
    private MosaicProviderRegistry() {}
    public static void register(MosaicProvider p) { PROVIDERS.add(p); }
    public static MosaicProvider forVersion(String mcVersion) {
        for (MosaicProvider p : PROVIDERS)
            if (p.mcVersion().equals(mcVersion)) return p;
        throw new mosaic.MosaicProviderNotFoundException(
                "no provider for MC version " + mcVersion + " (registered: "
                + PROVIDERS.stream().map(MosaicProvider::mcVersion).toList() + ")");
    }
    public static java.util.List<MosaicProvider> all() { return new java.util.ArrayList<>(PROVIDERS); }
}
