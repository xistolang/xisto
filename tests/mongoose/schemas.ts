// The documents of tests/mongoose/main.ts: @Schema classes with the @Prop forms of a NestJS app.
import { Prop, Schema, SchemaFactory } from '@nestjs/mongoose';
import { HydratedDocument, SchemaTypes, Types } from 'mongoose';

export const STATUSES = ['ATIVO', 'CONCLUIDO', 'CANCELADO'] as const;
export type RolloutStatus = (typeof STATUSES)[number];

@Schema({ _id: false })
export class Settings {
  @Prop({ type: String, enum: ['light', 'dark', 'system'], required: false })
  theme?: string;

  @Prop({ type: Boolean, required: false })
  compact?: boolean;
}
export const SettingsSchema = SchemaFactory.createForClass(Settings);

@Schema({ collection: 'rollouts', timestamps: true })
export class Rollout {
  @Prop({ type: String, required: true, index: true })
  nome: string;

  @Prop({ type: String, enum: STATUSES, default: 'ATIVO' })
  status: RolloutStatus;

  @Prop({ type: Types.ObjectId, ref: 'Group', default: null })
  grupoId: Types.ObjectId | null;

  @Prop({ type: [String], required: true, default: [] })
  membros: string[];

  @Prop({ type: Number, default: 100, min: 1, max: 100 })
  pcOnda: number;

  @Prop({ type: Boolean, default: false })
  fixado: boolean;

  @Prop({ type: Date, default: null })
  encerradoEm: Date | null;

  @Prop({ type: SchemaTypes.Mixed, required: false })
  extra?: unknown;

  @Prop({ type: String, required: false, lowercase: true, trim: true, unique: true, sparse: true })
  email?: string;

  @Prop({ type: SettingsSchema, required: true, default: () => ({}) })
  settings: Settings;

  @Prop({ type: Date, required: true, default: () => new Date() })
  ts: Date;

  createdAt?: Date;
  updatedAt?: Date;
}
export type RolloutDocument = HydratedDocument<Rollout>;
export const RolloutSchema = SchemaFactory.createForClass(Rollout);
RolloutSchema.index({ status: 1 });
RolloutSchema.index({ membros: 1, status: 1 }, { unique: true });
RolloutSchema.index({ 'settings.theme': 1, ts: -1 });
RolloutSchema.index({ ts: 1 }, { expireAfterSeconds: 60 * 60 * 24 * 365 });

@Schema({ collection: 'events' })
export class FleetEvent {
  @Prop({ type: String, required: true })
  nmMaquina: string;

  @Prop({ type: Date, required: true })
  em: Date;
}
export const FleetEventSchema = SchemaFactory.createForClass(FleetEvent);

