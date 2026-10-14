# PS5 Payload Manager Translations

PS5 Payload Manager supports multiple languages to provide an accessible and intuitive experience for users worldwide.

Translation files live in `frontend/src/locales/`. Some strings have been
prepared with AI assistance; community corrections remain welcome. Structural
checks do not certify that wording is natural or correct. All 17 bundled locales
are available, including Dutch; there is no percentage-based language-hiding rule.

## Maintaining Translations

Use stable keys from the active UI and preserve `{{interpolation}}` placeholders
and React translation markup such as `<1>...</1>` or `<strong>...</strong>`.
Provide each language's cardinal forms (`_one`, `_few`, `_many`, `_other`, etc.)
for count labels. Product names and technical terms can legitimately match English.
Only active features should have translation keys. Self-update uses the Get
Payloads download notice; the former notification toggle/banner and Disc Player
auto-close remain absent.

From `frontend/`, `npm run extract` updates the **English** catalog only. Review
its diff before keeping it, especially dynamic categories and plural entries;
it does not translate other locales. Update those locales explicitly, then run
`npm test` and `npm run build`. The locale suite checks active source-key coverage,
placeholders, markup, required plural forms and obsolete entries. Browser checks
include Dutch selection, reload persistence, fallback and representative locales.
Native-speaker wording review and console layout testing remain useful.

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
