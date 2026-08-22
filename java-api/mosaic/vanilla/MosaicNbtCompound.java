package mosaic.vanilla;

public interface MosaicNbtCompound {
    boolean contains(String key);
    String getString(String key);
    int getInt(String key);
    void putString(String key, String value);
    void putInt(String key, int value);
    String[] keys();
    byte[] toBytes();
}
