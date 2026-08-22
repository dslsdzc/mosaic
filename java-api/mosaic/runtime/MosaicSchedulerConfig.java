package mosaic.runtime;

public interface MosaicSchedulerConfig {
    int workers();
    static MosaicSchedulerConfig of(int workers) { return new MosaicSchedulerConfig() {
        public int workers() { return workers; } }; }
}
