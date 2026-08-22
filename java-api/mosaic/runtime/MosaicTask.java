package mosaic.runtime;

public interface MosaicTask {
    long id();
    int[] dependencyIds();
    int priority();
    int affinity();
    void run();
    MosaicCheckpoint checkpoint();
}
