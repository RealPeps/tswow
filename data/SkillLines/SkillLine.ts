import { SkillLineRow } from "wotlkdata/dbc/types/SkillLine";
import { MainEntity } from "../Base/MainEntity";
import { SkillRaceClassInfos } from "./SkillRaceClassInfo";

export class SkillLine extends MainEntity<SkillLineRow> {
    get AlternateVerb() { return this.wrapLoc(this.row.AlternateVerb); }
    get CanLink() { return this.wrap(this.row.CanLink); }
    get Category() { return this.wrap(this.row.CategoryID); }
    get Description() { return this.wrapLoc(this.row.Description); }
    get Name() { return this.wrapLoc(this.row.DisplayName); }
    get ID() { return this.row.ID.get(); }
    get SkillCosts() { return this.wrap(this.row.SkillCostsID); }
    get SpellIcon() { return this.wrap(this.row.SpellIconID); }
    get RaceClassInfos() { return new SkillRaceClassInfos(this); }
}