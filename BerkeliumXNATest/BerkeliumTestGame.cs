using System;
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Graphics;
using Microsoft.Xna.Framework.Input;
using Berkelium.Managed;
using System.Runtime.InteropServices;
using System.IO;
using System.Text;

namespace BerkeliumXNATest {
    public class BerkeliumTestGame : Game {
        [DllImport("user32.dll")]
        static extern int ToUnicode (
            uint wVirtKey, uint wScanCode, byte[] lpKeyState,
            [Out, MarshalAs(UnmanagedType.LPWStr, SizeConst = 64)] StringBuilder pwszBuff, 
            int cchBuff, uint wFlags
        );

        Texture2D background, oldPage;
        RenderTarget2D oldPageRt;
        GraphicsDeviceManager graphics;
        SpriteBatch spriteBatch;
        TextureBackedWindow browser;
        int fadeDirection = 0;
        long? fadingSince = null;
        long lastKeystrokeTime = 0;

        MouseState oldMouseState;
        KeyboardState oldKeyState;

        public BerkeliumTestGame () {
            graphics = new GraphicsDeviceManager(this);
            Content.RootDirectory = "Content";

            graphics.PreferredBackBufferWidth = 1024;
            graphics.PreferredBackBufferHeight = 600;
            graphics.PreferredBackBufferFormat = SurfaceFormat.Bgr32;
            graphics.PreferredDepthStencilFormat = DepthFormat.Depth16;
            graphics.PreferMultiSampling = false;

#if !DEBUG_BERKELIUM
            BerkeliumSharp.Init(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "BerkeliumXNATest"));
#else
			BerkeliumSharp.Init(@"D:\build\berkelium-managed\bin");
#endif
		}

        protected override void Initialize () {
            base.Initialize();

            System.Windows.Forms.Cursor.Show();
        }

        
        int NextPowerOfTwo(int value) {
            double v = Math.Log10((double)value) / Math.Log10(2.0);
            if (Math.Floor(v) != Math.Ceiling(v)) {
                return 1 << (int)Math.Ceiling(v);
            }
            return value;
        }

        protected override void LoadContent () {
            // We have to inject our teardown code here because if we let XNA's
            //  normal disposal handlers run, Chrome's message pump gets confused
            //  and hangs our process
            var form = (System.Windows.Forms.Control.FromHandle(Window.Handle) as System.Windows.Forms.Form);
            form.FormClosing += (s, e) => Teardown();

            spriteBatch = new SpriteBatch(GraphicsDevice);

            background = Content.Load<Texture2D>("background");

            var context = Context.Create();

            browser = new TextureBackedWindow(context, GraphicsDevice);
            browser.Transparent = true;
            browser.LoadingStateChanged += WebKit_LoadingStateChanged;
            browser.Load += WebKit_Load;
            browser.AddressBarChanged += WebKit_AddressBarChanged;
            browser.Resize(GraphicsDevice.Viewport.Width, GraphicsDevice.Viewport.Height);

            int rtWidth = browser.Texture.Width;
            int rtHeight = browser.Texture.Height;

            try {
                oldPageRt = new RenderTarget2D(
                    GraphicsDevice, rtWidth, rtHeight, 2,
                    SurfaceFormat.Color, RenderTargetUsage.DiscardContents
                );
            } catch {
                rtWidth = NextPowerOfTwo(rtWidth);
                rtHeight = NextPowerOfTwo(rtHeight);
                oldPageRt = new RenderTarget2D(
                    GraphicsDevice, rtWidth, rtHeight, 2,
                    SurfaceFormat.Color, RenderTargetUsage.DiscardContents
                );
            }

            browser.NavigateTo("http://www.google.com/");
        }

        void WebKit_Load (Window window) {
        }

        void WebKit_AddressBarChanged (Window window, string newUrl) {
        }

        private void CopyTextureToRenderTarget (Texture2D src, RenderTarget2D rt, ref Texture2D dest) {
            var dsb = GraphicsDevice.DepthStencilBuffer;
            GraphicsDevice.DepthStencilBuffer = null;
            GraphicsDevice.SetRenderTarget(0, rt);
            spriteBatch.Begin(SpriteBlendMode.None, SpriteSortMode.Immediate, SaveStateMode.None);
            spriteBatch.Draw(src, new Rectangle(0, 0, src.Width, src.Height), Color.White);
            spriteBatch.End();
            GraphicsDevice.SetRenderTarget(0, null);
            GraphicsDevice.DepthStencilBuffer = dsb;
            dest = rt.GetTexture();
            dest.GenerateMipMaps(TextureFilter.Linear);
        }

        private void WebKit_LoadingStateChanged (Window window, bool newState) {
            if (newState == true)
                CopyTextureToRenderTarget(browser.Texture, oldPageRt, ref oldPage);

            int newDirection = newState ? -1 : 1;
            if (fadeDirection != newDirection) {
                fadeDirection = newDirection;
                fadingSince = DateTime.UtcNow.Ticks;
            }
        }

        private void WebKit_LoadError (Window window, string error) {
            Console.WriteLine(error);
        }

        protected override void UnloadContent () {
        }

        public void Teardown () {
            browser.Dispose();
            BerkeliumSharp.Destroy();
        }

        protected override void Update (GameTime gameTime) {
            var newMouseState = Mouse.GetState();
            var newKeyState = Keyboard.GetState();

            var scrollDelta = newMouseState.ScrollWheelValue - oldMouseState.ScrollWheelValue;

            if ((newMouseState.X != oldMouseState.X) || (newMouseState.Y != oldMouseState.Y))
                browser.MouseMoved(newMouseState.X, newMouseState.Y);

            if (scrollDelta != 0)
                browser.MouseWheel(0, scrollDelta);

            if (newMouseState.LeftButton != oldMouseState.LeftButton)
                browser.MouseButton(MouseButton.Left, newMouseState.LeftButton == ButtonState.Pressed);

            if (newMouseState.MiddleButton != oldMouseState.MiddleButton)
                browser.MouseButton(MouseButton.Middle, newMouseState.MiddleButton == ButtonState.Pressed);

            if (newMouseState.RightButton != oldMouseState.RightButton)
                browser.MouseButton(MouseButton.Right, newMouseState.RightButton == ButtonState.Pressed);

            var buffer = new StringBuilder();
            byte[] pressedKeys = new byte[256];

            long now = DateTime.UtcNow.Ticks;
            bool shouldRepeat = (now - lastKeystrokeTime) > TimeSpan.FromSeconds(0.15).Ticks;

            for (int i = 0; i < 255; i++) {
                var k = (Keys)i;
                bool wasDown = oldKeyState.IsKeyDown(k);
                bool isDown = newKeyState.IsKeyDown(k);
                if ((wasDown != isDown) || (isDown && shouldRepeat)) {
                    lastKeystrokeTime = now;
                    browser.KeyEvent(isDown, 0, i, 0);

                    if (isDown) {
                        Array.Clear(pressedKeys, 0, 256);

                        foreach (var pk in newKeyState.GetPressedKeys()) {
                            int ik = (int)pk;

                            if ((pk == Keys.LeftShift) || (pk == Keys.RightShift))
                                ik = 0x0010;

                            pressedKeys[ik] = 255;
                        }

                        int chars = ToUnicode((uint)i, 0, pressedKeys, buffer, 1, 0);
                        if (chars > 0)
                            browser.TextEvent(buffer.ToString(0, chars));
                    }
                }
            }

            oldMouseState = newMouseState;
            oldKeyState = newKeyState;

            browser.Cleanup();

            if (browser != null)
                BerkeliumSharp.Update();

            base.Update(gameTime);
        }
        
        protected override void Draw (GameTime gameTime) {
            GraphicsDevice.Clear(Color.Black);

            int a = 511;
            float blur = 0.0f;
            long now = DateTime.UtcNow.Ticks;
            long elapsed = now - fadingSince.GetValueOrDefault(0);
            if (fadeDirection > 0) {
                a = (int)MathHelper.Clamp(511 * elapsed / TimeSpan.FromSeconds(0.33).Ticks, 0, 511);
                blur = 1.0f;
            } else if (fadeDirection < 0) {
                a = 0;
                blur = MathHelper.Clamp(100 * elapsed / TimeSpan.FromSeconds(0.33).Ticks, 0, 100) / 100.0f;
            }

            spriteBatch.Begin(SpriteBlendMode.AlphaBlend, SpriteSortMode.Immediate, SaveStateMode.None);
            spriteBatch.Draw(background, new Rectangle(0, 0, GraphicsDevice.Viewport.Width, GraphicsDevice.Viewport.Height), Color.White);

            if ((fadeDirection < 0) || (a < 511)) {
                spriteBatch.End();

                spriteBatch.Begin(SpriteBlendMode.AlphaBlend, SpriteSortMode.Immediate, SaveStateMode.None);
                GraphicsDevice.SamplerStates[0].MinFilter = TextureFilter.Linear;
                GraphicsDevice.SamplerStates[0].MipFilter = TextureFilter.Linear;
                GraphicsDevice.SamplerStates[0].MagFilter = TextureFilter.Linear;
                GraphicsDevice.SamplerStates[0].MipMapLevelOfDetailBias = blur;

                byte a2;
                if (fadeDirection > 0) {
                    a2 = (byte)MathHelper.Clamp(255 - (a - 256), 0, 255);
                } else {
                    a2 = 255;
                }

                spriteBatch.Draw(
                    oldPage,
                    new Rectangle(0, 0, browser.Texture.Width, browser.Texture.Height),
                    new Rectangle(0, 0, browser.Texture.Width, browser.Texture.Height),
                    new Color(255, 255, 255, a2)
                );
                spriteBatch.End();

                spriteBatch.Begin(SpriteBlendMode.AlphaBlend, SpriteSortMode.Immediate, SaveStateMode.None);
                GraphicsDevice.SamplerStates[0].MinFilter = TextureFilter.Point;
                GraphicsDevice.SamplerStates[0].MipFilter = TextureFilter.Point;
                GraphicsDevice.SamplerStates[0].MagFilter = TextureFilter.Point;
                GraphicsDevice.SamplerStates[0].MipMapLevelOfDetailBias = 0.0f;
            }

            if (fadeDirection >= 0) {
                foreach (var kvp in browser.RenderList) {
                    spriteBatch.Draw(
                        kvp.Key,
                        new Rectangle(kvp.Value.X, kvp.Value.Y, kvp.Key.Width, kvp.Key.Height),
                        new Color(255, 255, 255, (byte)MathHelper.Clamp(a, 0, 255))
                    );
                }
            }

            spriteBatch.End();

            base.Draw(gameTime);
        }
    }
}
