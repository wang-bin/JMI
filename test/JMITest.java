public class JMITest {
    static {
        System.loadLibrary("JMITest");
    }
 
    private native void nativeTest();
    public static void main(String[] args) {
        new JMITest().nativeTest();  // invoke the native method
    }

    public int x;
    public static int y = 168;
    public String str = "text";
    public static String sstr = "static text";
}
