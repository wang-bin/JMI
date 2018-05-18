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
    public String[] getStrArray() {
        String[] ss = new String[2];
        ss[0] = str;
        ss[1] = sstr;
        return ss;
    }
    public static String[] getStrArrayS() {
        String[] ss = new String[2];
        ss[0] = sstr;
        return ss;
    }

    public int[] getIntArray() {
        int[] a = new int[2];
        a[0] = 1;
        a[1] = x;
        return a;
    }

    public void getIntArrayAsParam(int[] a) {
        a[0] = 1;
        a[1] = x;
    }
    public JMITest getSelf() {
        return this;
    }
    public void getSelfArray(JMITest[] v) {
        v[0] = this;
        v[1] = new JMITest();
    }

    private int x;
    private static int y = 168;
    private String str = "text";
    private static String sstr = "static text";
    public JMITest self = this;
}
