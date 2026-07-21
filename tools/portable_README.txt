bf-radio portable field kit
===========================

This package is portable. Unzip it and run the tools from this directory.
It does not install anything and does not require adding anything to PATH.

First-time setup
----------------

1. Plug in Digirig.
2. Open PowerShell or cmd.exe in this unpacked directory.
3. Generate the local config:

   .\bin\digirig_config.exe --write-config .\config\digirig.cfg --yes

Transmit text from a file
-------------------------

Create a message file:

   echo sp5mpp test dwukomputerowy sp5mpp>message.txt

Transmit it:

   .\bin\bf_text_tx.exe --config .\config\digirig.cfg --output-level 0.3 --verbose message.txt

Receive and decode
------------------

Record for 30 seconds in short 3-second segments, stitch them, and decode:

   .\bin\bf_chirp_rx.exe --config .\config\digirig.cfg --record-seconds 30 --segment-seconds 3 --verbose rx\capture

Decode existing recorded segments again:

   .\bin\bf_chirp_rx.exe --decode-only --output decoded.txt rx\capture

Useful tools
------------

bin\digirig_config.exe  - detect Digirig serial/audio devices and write config
bin\bf_text_tx.exe      - encode a file with the chirp modem and transmit it
bin\bf_chirp_rx.exe     - record/stitch/decode chirp transmissions
bin\bf_recorder.exe     - raw segmented recorder
bin\bf_wav_tx.exe       - transmit an existing WAV via Digirig PTT
bin\chirp_modem.exe     - original lab chirp modem CLI
bin\pcm_to_wav.exe      - raw PCM to WAV helper
bin\wav_to_pcm.exe      - WAV to raw PCM helper

Notes
-----

The default Digirig PTT polarity in config\digirig.cfg is active-high:

   ptt_active_low=false

If your interface behaves inverted, change it or use:

   --ptt-active-low
