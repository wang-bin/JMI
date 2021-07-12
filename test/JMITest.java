import java.lang.StringBuffer;

public class JMITest {
    static {
        System.loadLibrary("JMITest");
    }

    private native void nativeTest();
    public static void main(String[] args) {
        new JMITest().nativeTest();  // invoke the native method
    }

    public static void resetStatic() {
        y = 168;
    }
    public void setX(int v) { x = v;}
    public int getX() { return x;}
    public static void setY(float v) { y = v;}
    public static float getY() { return y;}
    public void setStr(String v) { str = v;}
    public String getStr() { return str;}
    public static void getSStr(String[] s) {
        s[0] = " output  String[]";
        sstr = "static text";
    }
    public static String getSub(int begin, int end, String s) {
        return s.substring(begin, end);
    }

    public String sub(int begin, int end) {
        return str.substring(begin, end);
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
    private static float y = 168;
    private String str = "text";
    private static String sstr = "static text";
    public JMITest self = this;
}
