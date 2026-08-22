package mosaic.runtime;

public interface MosaicActivationGate {
    MosaicActivationPolicy policy();
    void setPolicy(MosaicActivationPolicy policy);
}
