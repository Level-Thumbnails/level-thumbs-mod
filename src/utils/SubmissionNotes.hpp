#pragma once
#include "UrlEncode.hpp"

namespace notes {
    inline int getLevelRating(GJGameLevel* level) {
        if (!level) return 0;
        int featured = level->m_featured;
        switch (level->m_isEpic) {
            default:
                if (level->m_stars == 0 && featured <= 0) {
                    return 0; // Unrated
                }
                return featured <= 0 ? 1 : 2; // Rated/Featured
            case 1: return 3; // Epic
            case 2: return 4; // Legendary
            case 3: return 5; // Mythic
        }
    }

    inline int getLevelDifficulty(GJGameLevel* level) {
        if (level->m_autoLevel) return 1; // Auto
        auto diff = level->m_difficulty;

        if (level->m_ratingsSum != 0)
            diff = static_cast<GJDifficulty>(level->m_ratingsSum / level->m_ratings);

        if (level->m_demon > 0) {
            switch (level->m_demonDifficulty) {
                case 3: return 7; // Easy Demon
                case 4: return 8; // Medium Demon
                case 5: return 10; // Insane Demon
                case 6: return 11; // Extreme Demon
                default: return 9; // Hard Demon
            }
        }

        switch (diff) {
            case GJDifficulty::Easy: return 2;
            case GJDifficulty::Normal: return 3;
            case GJDifficulty::Hard: return 4;
            case GJDifficulty::Harder: return 5;
            case GJDifficulty::Insane: return 6;
            case GJDifficulty::Demon: return 9;
            default: return 0;
        }
    }

    inline static double getAccurateProgress(PlayLayer* pl) {
        auto progress = std::clamp(
            pl->m_level->m_timestamp > 0
                ? pl->m_gameState.m_levelTime * 240.0 / pl->m_level->m_timestamp * 100.0
                : pl->m_player1->getPositionX() * 100.0 / pl->m_levelLength,
            0.0, 100.0
        );

        if (!std::isfinite(progress)) {
            return pl->getCurrentPercent();
        }

        return progress;
    }

    inline static std::string buildSubmissionNote() {
        auto playLayer = PlayLayer::get();
        if (!playLayer || !playLayer->m_level) {
            return "";
        }

        auto level = playLayer->m_level;

        StringBuffer sb;

        // Level Info
        sb.append("v=1;");
        sb.append("ln=");
        urlEncodeAppend(sb, level->m_levelName);
        sb.append(";");
        sb.append("ci={};", level->m_accountID);
        sb.append("cn={};", level->m_creatorName);
        sb.append("dw={};", level->m_downloads);
        sb.append("lk={};", level->m_likes);
        sb.append("ls={};", level->m_stars);
        sb.append("ll={};", level->m_levelLength);
        sb.append("lr={};", getLevelRating(level));
        sb.append("ld={};", getLevelDifficulty(level));

        // Screenshot Info
        sb.append("pr={};", getAccurateProgress(playLayer));
        sb.append("tm={};", playLayer->m_gameState.m_levelTime);

        return sb.str();
    }
}