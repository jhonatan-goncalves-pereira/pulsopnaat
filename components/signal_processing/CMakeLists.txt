idf_component_register(SRCS "signal_processing.c"
                       INCLUDE_DIRS "include"
                       # esp-dsp: FFT (dsps_fft2r) e janela de Hann
                       # (dsps_wind_hann) — dependência PRIVADA: os headers
                       # públicos deste componente não expõem esp-dsp.
                       PRIV_REQUIRES esp-dsp)
