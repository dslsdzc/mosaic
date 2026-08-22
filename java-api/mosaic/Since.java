package mosaic;

import java.lang.annotation.*;

/** 标注 API 成员引入版本(只增不减:成员引入后永不删除/改语义)。 */
@Retention(RetentionPolicy.RUNTIME)
@Target({ElementType.METHOD, ElementType.TYPE, ElementType.FIELD})
public @interface Since {
    int value();
}
