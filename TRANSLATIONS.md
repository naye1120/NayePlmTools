# PS5 Payload Manager Translations

PS5 Payload Manager supports multiple languages to provide an accessible and intuitive experience for users worldwide.

Translation files live in `frontend/src/locales/`. Community contributions and corrections are always welcome.

## Maintaining Translations

- Use stable translation keys matching the active UI.
- Preserve `{{interpolation}}` placeholders and React translation markup such as `<1>...</1>` or `<strong>...</strong>`.
- Provide each language's cardinal forms (`_one`, `_few`, `_many`, `_other`, etc.) for count labels based on that language's plural rules.
- Technical terms, acronyms, and product names (such as ELF, PID, PS5, etc.) can legitimately remain in English where conventional.
- Only active features should have translation keys.

### Workflow

1. From `frontend/`, run `npm run extract` to update the **English** catalog (`en-US.json`) with any new UI keys.
2. Update the respective locale files explicitly in `frontend/src/locales/`.
3. If adding a new locale:
   - Register its native display name in `frontend/src/utils/localeSupport.js` (`LANGUAGE_NAMES`).
   - If older WebKit / PS5 browsers require fallback cardinal rules without `Intl.PluralRules`, define them in `fallbackCardinalRule`.
   - Add the language to the Supported Languages table below.
4. Run `npm test` and `npm run build` from `frontend/` to verify that key coverage, placeholders, markup, and plural forms are valid.

---

## Reporting Translation Issues

If you find any inaccurate, unnatural, or missing translations, we welcome your feedback!

Please [open a new Issue](https://github.com/itsPLK/ps5-payload-manager/issues/new) and describe:
1. The language and the screen/button where the text appears.
2. The current translation.
3. Your proposed correction and the reasoning.

---

## Supported Languages

| Language | Code |
| :--- | :---: |
| **Arabic** | `ar-SA` |
| **Chinese Simplified** | `zh-CN` |
| **Chinese Traditional** | `zh-TW` |
| **Dutch** | `nl-NL` |
| **English** | `en-US` |
| **French** | `fr-FR` |
| **German** | `de-DE` |
| **Italian** | `it-IT` |
| **Japanese** | `ja-JP` |
| **Korean** | `ko-KR` |
| **Persian** | `fa-IR` |
| **Polish** | `pl-PL` |
| **Portuguese (Brazilian)** | `pt-BR` |
| **Romanian** | `ro-RO` |
| **Russian** | `ru-RU` |
| **Spanish** | `es-ES` |
| **Thai** | `th-TH` |
| **Turkish** | `tr-TR` |
| **Ukrainian** | `uk-UA` |

---

## Past Community Translators

We are immensely grateful to all community members who helped translate earlier versions of the project:

- **Arabic**: [mohamad.z.k28](https://crowdin.com/profile/mohamad.z.k28), [abdoulacadat](https://crowdin.com/profile/abdoulacadat), [DumDum143](https://crowdin.com/profile/DumDum143), [HoussamLovy](https://crowdin.com/profile/HoussamLovy), [c0ncerta](https://crowdin.com/profile/c0ncerta), [sgs2ofameer](https://crowdin.com/profile/sgs2ofameer)
- **Chinese Simplified**: [owendswang](https://crowdin.com/profile/owendswang), [carlchina](https://crowdin.com/profile/carlchina)
- **Dutch**: [JensJ56](https://crowdin.com/profile/JensJ56)
- **French**: [Oblik20](https://crowdin.com/profile/Oblik20), [Chronoss](https://crowdin.com/profile/Chronoss)
- **German**: [stylefile](https://crowdin.com/profile/stylefile), [xEasy4Breezy](https://crowdin.com/profile/xEasy4Breezy), [FENDHOUSE](https://crowdin.com/profile/FENDHOUSE), [baschte](https://crowdin.com/profile/baschte), [mighel88](https://crowdin.com/profile/mighel88)
- **Italian**: [Leon90](https://crowdin.com/profile/Leon90)
- **Japanese**: [Jane101KR](https://crowdin.com/profile/Jane101KR)
- **Korean**: [Jane101KR](https://crowdin.com/profile/Jane101KR), [pop9871517](https://crowdin.com/profile/pop9871517)
- **Persian**: [Epinor](https://crowdin.com/profile/Epinor), [FCGre](https://crowdin.com/profile/FCGre)
- **Polish**: [najdek](https://crowdin.com/profile/najdek)
- **Portuguese (Brazilian)**: [matmarson](https://crowdin.com/profile/matmarson), [slipttees](https://crowdin.com/profile/slipttees), [marcusvrb](https://crowdin.com/profile/marcusvrb)
- **Russian**: [Akela-1979](https://crowdin.com/profile/Akela-1979)
- **Spanish**: [Slayver95](https://crowdin.com/profile/Slayver95), [kXmpX](https://crowdin.com/profile/kXmpX), [sagatgs](https://crowdin.com/profile/sagatgs)
- **Thai**: [Modmycon](https://crowdin.com/profile/Modmycon), [hpokemon776](https://crowdin.com/profile/hpokemon776)
- **Turkish**: [Sezar61](https://crowdin.com/profile/Sezar61), [hpokemon776](https://crowdin.com/profile/hpokemon776)
- **Ukrainian**: [Mikeeee](https://crowdin.com/profile/Mikeeee)
