package mosaic.vanilla;

@FunctionalInterface
public interface MosaicCommandHandler {
    int execute(String[] args);
}
