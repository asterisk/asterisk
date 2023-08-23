"""drop unused codec prefs settings

This is an exact reversal of the migration introduced by 9f3692b1654b.

Revision ID: 2b50def7afd2
Revises: f5b0e7427449
Create Date: 2023-08-23 16:32:11.664602

"""

# revision identifiers, used by Alembic.
revision = '2b50def7afd2'
down_revision = 'f5b0e7427449'

from alembic import op
import sqlalchemy as sa
from sqlalchemy.dialects.postgresql import ENUM

PJSIP_INCOMING_CALL_OFFER_PREF_NAME ='pjsip_incoming_call_offer_pref_values'
PJSIP_INCOMING_CALL_OFFER_PREF_VALUES = ['local', 'local_first',
                                         'remote', 'remote_first']

PJSIP_OUTGOING_CALL_OFFER_PREF_NAME = 'pjsip_outgoing_call_offer_pref_values'
PJSIP_OUTGOING_CALL_OFFER_PREF_VALUES = ['local', 'local_merge', 'local_first',
                                         'remote', 'remote_merge', 'remote_first']

def upgrade():
    context = op.get_context()

    if context.bind.dialect.name == 'mssql':
        op.drop_constraint('ck_ps_endpoints_incoming_call_offer_pref_pjsip_incoming_call_offer_pref_values', 'ps_endpoints')
        op.drop_constraint('ck_ps_endpoints_outgoing_call_offer_pref_pjsip_outgoing_call_offer_pref_values', 'ps_endpoints')

    op.drop_column('ps_endpoints', 'outgoing_call_offer_pref')
    op.drop_column('ps_endpoints', 'incoming_call_offer_pref')
    op.drop_column('ps_endpoints', 'stir_shaken_profile')

    enums = [PJSIP_INCOMING_CALL_OFFER_PREF_NAME, PJSIP_OUTGOING_CALL_OFFER_PREF_NAME]

    if context.bind.dialect.name == 'postgresql':
        for e in enums:
            ENUM(name=e).drop(op.get_bind(), checkfirst=False)

def downgrade():
    context = op.get_context()

    if context.bind.dialect.name == 'postgresql':
        enum_in = ENUM(*PJSIP_INCOMING_CALL_OFFER_PREF_VALUES, name=PJSIP_INCOMING_CALL_OFFER_PREF_NAME)
        enum_out = ENUM(*PJSIP_OUTGOING_CALL_OFFER_PREF_VALUES, name=PJSIP_OUTGOING_CALL_OFFER_PREF_NAME)

        enum_in.create(op.get_bind(), checkfirst=False)
        enum_out.create(op.get_bind(), checkfirst=False)

    op.add_column('ps_endpoints', sa.Column('incoming_call_offer_pref',
            sa.Enum(*PJSIP_INCOMING_CALL_OFFER_PREF_VALUES, name=PJSIP_INCOMING_CALL_OFFER_PREF_NAME)))

    op.add_column('ps_endpoints', sa.Column('outgoing_call_offer_pref',
            sa.Enum(*PJSIP_OUTGOING_CALL_OFFER_PREF_VALUES, name=PJSIP_OUTGOING_CALL_OFFER_PREF_NAME)))

    op.add_column('ps_endpoints', sa.Column('stir_shaken_profile', sa.String(80)))
