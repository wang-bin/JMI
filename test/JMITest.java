import java.lang.StringBuffer;

public class JMITest {
    static {
        System.loadLibrary("JMITest");
    }
 
    private native void nativeTest();
    public static void main(String[] args) {
        new JMITest().nativeTest();  // invoke the native method
    }

    public void setX(int v) { x = v;}
    public int getX() { return x;}
    public static void setY(int v) { y = v;}
    public static int getY() { return y;}
    public void setStr(String v) { str = v;}
    public String getStr() { return str;}
    public static void getSStr(String[] s) {
        s[0] = " output  String[]";
    }
    public void getIntArray(int[] a) {
        a[0] = 1;
        a[1] = x;
    }
    public JMITest getSelf() {
        return this;
    }

    private int x;
    private static int y = 168;
    private String str = "text";
    private static String sstr = "static text";
    public JMITest self = this;
}
