# NoteRoom 🖍️🌐
**A real-time, global online drawing and text messenger for the Nintendo 3DS.**

Inspired by classic local drawing messengers from the golden era of handheld gaming, NoteRoom brings the nostalgia of dual-screen chatting into the modern era by connecting 3DS players worldwide over the internet.

⚠️ **WARNING:** This GitHub repository (`github.com/SprtnDio/NoteRoom`) is the **ONLY official source** for NoteRoom. Do not download `.cia` files from random websites or third-party Discord servers to avoid malware or modified clones.

## ✨ Features
* **Draw & Text:** Send up to 150-character text messages or draw doodles in real-time.
* **Dynamic Lobbies:** Join themed channels like *Main Plaza* (separated by languages), *FC Exchange*, *Tech Support*, or find gaming buddies in the *Matchmaking* lobbies.
* **Live User Radar:** See exactly how many users are active in each lobby before you even join a room.
* **Hardware-Optimized UI:** Smooth analog stick scrolling, variable pen/eraser sizes, and a dedicated auto-scroll toggle.
* **Safe Environment:** The official NoteRoom server is backed by an AI-assisted moderation tool. It scans for severe NSFW drawings and toxic language in real-time. However, the AI acts purely as an assistant tool—**all final moderation and ban decisions are made by human admins.**
* **Anti-Spam Measures:** Hard limits on ink (650 strokes per message) and a robust hardware-ID ban system prevent trolls from ruining the fun.

---

## 📥 Installation (For Players)
If you just want to use the app and chat with others, you do **not** need to compile the code.
1. Go to the [Releases](../../releases) tab on the right side of this GitHub page.
2. Download the latest `NoteRoom.cia` file.
3. Copy the `.cia` to your SD card and install it using **FBI**.
4. Launch the app from your 3DS Home Menu!

---

## 🛠️ Building from Source (For Developers)
If you want to compile NoteRoom yourself or host your own private server, follow these steps. 
*(Note: To protect the official server, the source code does not include the official server credentials).*

**Prerequisites:**
You need a working installation of [devkitPro](https://devkitpro.org/) with `devkitARM`, `libctru`, `citro2d`, `citro3d`, as well as `makerom` and `bannertool`.

**Step-by-Step:**
1. Clone this repository.
2. Rename the file `secrets.example.h` to `secrets.h`.
3. Open `secrets.h` and enter your own MQTT Broker IP/Domain and Port (you can use an XOR cipher if you want to obfuscate them, check `main.c` for the decryption logic).
4. Open your terminal in the project directory and run:
   ```bash
   make clean
   make
1. You will get a custom NoteRoom.cia to install on your 3DS.

📜 License
This project is licensed under the GNU General Public License v3.0 (GPLv3).
  See the LICENSE file for more details. If you fork or modify this code and distribute compiled binaries, 
  you are legally required to open-source your modified code and clearly state that it is a modified version.

⚖️ Legal Disclaimer & Liability (Please Read)

1. No Affiliation with Nintendo:
NoteRoom is an unofficial, fan-made, non-profit homebrew application. It is not affiliated with, endorsed, sponsored,
or approved by Nintendo Co., Ltd. "Nintendo" and "Nintendo 3DS" are registered trademarks of Nintendo.

3. "As-Is" Software:
This software is provided "as is", without warranty of any kind, express or implied.
By installing and using NoteRoom, you accept that the author (SprtnDio) is not liable for any potential damage to your console,
loss of data, or any other issues that may arise from using this homebrew application.

5. User-Generated Content:
NoteRoom is a live, online communication tool. While the official server employs AI-assisted tools to help human moderators manage the chat,
it is impossible to catch 100% of inappropriate user behavior instantly. The author takes no responsibility and assumes no liability for any content, text,
or drawings generated, transmitted, or displayed by users within the app. Users interact with strangers at their own risk.

7. Privacy & Moderation:
To enforce bans and prevent repeated spam, the official NoteRoom server hashes and temporarily processes a unique hardware identifier
when you send a message. This data is strictly used for moderation purposes only to protect the community. No personal data is sold or shared.   
