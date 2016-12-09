public class JMITest {
    static {
        System.loadLibrary("JMITest");
    }
 
    private native void nativeTest();
    public static void main(String[] args) {
        new JMITest().nativeTest();  // invoke the native method
    }
}
